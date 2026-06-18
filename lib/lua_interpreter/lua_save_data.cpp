// ============================================
// ファイル: lua_save_data.cpp
// machine.save_data / load_data 実装
// ============================================

#include "lua_interpreter.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "heap_budget.hpp"

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "lauxlib.h"
#include "lua.h"
}

namespace {

constexpr size_t kMaxSaveBytes = 16 * 1024;
constexpr int kMaxNestDepth = 16;

/** Lua 文字列をエスケープしてダブルクォートで囲んだ形式に追記する */
void appendEscapedString(const char* s, std::string* out) {
    out->push_back('"');
    if (s) {
        for (const char* p = s; *p; ++p) {
            switch (*p) {
                case '\\':
                    out->append("\\\\");
                    break;
                case '"':
                    out->append("\\\"");
                    break;
                case '\n':
                    out->append("\\n");
                    break;
                case '\r':
                    out->append("\\r");
                    break;
                case '\t':
                    out->append("\\t");
                    break;
                default:
                    out->push_back(*p);
                    break;
            }
        }
    }
    out->push_back('"');
}

/** Lua テーブルキーを Lua リテラル形式の文字列に追記する */
bool appendKey(lua_State* L, int key_idx, std::string* out, char* err, size_t err_len) {
    const int t = lua_type(L, key_idx);
    if (t == LUA_TNUMBER) {
        if (lua_isinteger(L, key_idx)) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[%lld]", static_cast<long long>(lua_tointeger(L, key_idx)));
            out->append(buf);
            return true;
        }
        std::snprintf(err, err_len, "non-integer number keys not supported");
        return false;
    }
    if (t == LUA_TSTRING) {
        out->push_back('[');
        appendEscapedString(lua_tostring(L, key_idx), out);
        out->push_back(']');
        return true;
    }
    std::snprintf(err, err_len, "unsupported key type");
    return false;
}

/** Lua 値を Lua リテラル形式に追記する（前方宣言） */
bool appendValue(lua_State* L, int idx, std::string* out, int depth, char* err, size_t err_len);

/** Lua テーブルを再帰的に Lua リテラル文字列へシリアライズする */
bool appendTable(lua_State* L, int idx, std::string* out, int depth, char* err, size_t err_len) {
    if (depth > kMaxNestDepth) {
        std::snprintf(err, err_len, "table nest too deep (max %d)", kMaxNestDepth);
        return false;
    }
    if (!lua_istable(L, idx)) {
        std::snprintf(err, err_len, "expected table");
        return false;
    }

    const int abs = lua_absindex(L, idx);
    out->append("{\n");
    bool first = true;

    lua_pushnil(L);
    while (lua_next(L, abs) != 0) {
        if (!first) {
            out->push_back(',');
        }
        first = false;
        out->push_back('\n');
        if (!appendKey(L, -2, out, err, err_len)) {
            lua_pop(L, 1);
            return false;
        }
        out->append(" = ");
        if (!appendValue(L, -1, out, depth + 1, err, err_len)) {
            lua_pop(L, 1);
            return false;
        }
        lua_pop(L, 1);
    }
    out->append("\n}");
    return true;
}

/** Lua 値（nil/bool/number/string/table）を Lua リテラル形式に追記する */
bool appendValue(lua_State* L, int idx, std::string* out, int depth, char* err, size_t err_len) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            out->append("nil");
            return true;
        case LUA_TBOOLEAN:
            out->append(lua_toboolean(L, idx) ? "true" : "false");
            return true;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(lua_tointeger(L, idx)));
                out->append(buf);
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", lua_tonumber(L, idx));
                out->append(buf);
            }
            return true;
        case LUA_TSTRING:
            appendEscapedString(lua_tostring(L, idx), out);
            return true;
        case LUA_TTABLE:
            return appendTable(L, idx, out, depth, err, err_len);
        default:
            std::snprintf(err, err_len, "unsupported value type (%s)", lua_typename(L, lua_type(L, idx)));
            return false;
    }
}

/** バイト列を SD ファイルへ書き込む（save_data 用） */
bool writeSdBytes(LuaInterpreter* interp, const char* path, const char* data, size_t len, char* err,
                  size_t err_len) {
    char norm[FF_LFN_BUF + 4];
    interp->resolveGamePath(path, norm, sizeof(norm));

    FIL file;
    const FRESULT fr = f_open(&file, norm, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        std::snprintf(err, err_len, "open failed (%s)", FRESULT_str(fr));
        return false;
    }
    UINT bw = 0;
    const FRESULT wr = f_write(&file, data, static_cast<UINT>(len), &bw);
    f_close(&file);
    if (wr != FR_OK || bw != len) {
        std::snprintf(err, err_len, "write failed (%s)", FRESULT_str(wr));
        return false;
    }
    printf("save_data: wrote %s (%u bytes)\n", norm, static_cast<unsigned>(len));
    return true;
}

/** SD からセーブファイルを読み込み、ヌル終端バッファとして返す */
bool readSdSaveBytes(LuaInterpreter* interp, const char* path, char** out_buf, size_t* out_len,
                     char* err, size_t err_len) {
    *out_buf = nullptr;
    *out_len = 0;

    char norm[FF_LFN_BUF + 4];
    interp->resolveGamePath(path, norm, sizeof(norm));

    FIL file;
    FRESULT fr = f_open(&file, norm, FA_READ);
    if (fr != FR_OK) {
        std::snprintf(err, err_len, "open failed (%s)", FRESULT_str(fr));
        return false;
    }
    const FSIZE_t fsize = f_size(&file);
    if (fsize == 0 || fsize > kMaxSaveBytes) {
        f_close(&file);
        std::snprintf(err, err_len, "invalid file size");
        return false;
    }
    const size_t alloc_size = static_cast<size_t>(fsize) + 1;
    void* alloc_ptr = nullptr;
    if (!HeapBudget::tryAlloc(alloc_size, &alloc_ptr)) {
        f_close(&file);
        std::snprintf(err, err_len, "heap budget exceeded");
        return false;
    }
    char* buf = static_cast<char*>(alloc_ptr);
    UINT br = 0;
    fr = f_read(&file, buf, static_cast<UINT>(fsize), &br);
    f_close(&file);
    if (fr != FR_OK || br != static_cast<UINT>(fsize)) {
        HeapBudget::release(buf, alloc_size);
        std::snprintf(err, err_len, "read failed");
        return false;
    }
    buf[fsize] = '\0';
    *out_buf = buf;
    *out_len = static_cast<size_t>(fsize);
    return true;
}

}  // namespace

/** Lua テーブルを Lua リテラル形式で SD に保存する */
bool LuaInterpreter::saveDataToSd(lua_State* L, int table_index, const char* path) {
    if (!L || !path || path[0] == '\0') {
        setLastError("save_data: invalid args");
        return false;
    }
    if (!sd_mounted_) {
        setLastError("SD not mounted");
        return false;
    }
    luaL_checktype(L, table_index, LUA_TTABLE);

    char err[96];
    err[0] = '\0';
    std::string body;
    body.reserve(512);
    if (!appendTable(L, table_index, &body, 0, err, sizeof(err))) {
        setLastError(err[0] ? err : "serialize failed");
        return false;
    }

    std::string file_data;
    file_data.reserve(body.size() + 64);
    file_data.append("-- game_machine save v1\nreturn ");
    file_data.append(body);
    file_data.push_back('\n');

    if (file_data.size() > kMaxSaveBytes) {
        setLastError("save data too large");
        return false;
    }

    if (!writeSdBytes(this, path, file_data.c_str(), file_data.size(), err, sizeof(err))) {
        setLastError(err);
        return false;
    }
    return true;
}

/** SD からセーブファイルを読み込み、Lua テーブルとしてスタックに push する */
bool LuaInterpreter::loadDataFromSd(lua_State* L, const char* path) {
    if (!L || !path || path[0] == '\0') {
        setLastError("load_data: invalid args");
        return false;
    }
    if (!sd_mounted_) {
        setLastError("SD not mounted");
        return false;
    }
    if (!sdFileExists(path)) {
        setLastError("file not found");
        return false;
    }

    char err[96];
    char* buf = nullptr;
    size_t len = 0;
    if (!readSdSaveBytes(this, path, &buf, &len, err, sizeof(err))) {
        setLastError(err);
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    const int load_stat = luaL_loadbuffer(L, buf, len, norm);
    HeapBudget::release(buf, len + 1);
    if (load_stat != LUA_OK) {
        const char* le = lua_tostring(L, -1);
        setLastError(le ? le : "load parse error");
        lua_pop(L, 1);
        return false;
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* pe = lua_tostring(L, -1);
        setLastError(pe ? pe : "load execute error");
        lua_pop(L, 1);
        return false;
    }

    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        setLastError("save file must return a table");
        return false;
    }
    return true;
}
