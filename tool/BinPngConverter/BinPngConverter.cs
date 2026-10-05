// ============================================================================
// BinPngConverter — 画像 (PNG/JPG/BMP/GIF) ⇔ RGB565 .bin 相互変換 ＋ WAV → 本体用 WAV 変換
// Suityouka Game Machine 用（machine.load_image / draw_bg_stream / メニュープレビュー）
//
//  .bin 形式: RGB565 リトルエンディアン、ヘッダなし（幅×高さ×2 バイト）
//  透過: マゼンタ 0xF81F をカラーキーとして扱う（machine.draw_image_keyed の既定）
//
//  ビルド（Windows に .NET Framework 4.x があれば実行可）:
//    mcs -codepage:65001 -target:winexe -sdk:4.5 -r:System.Windows.Forms.dll -r:System.Drawing.dll \
//        -out:BinPngConverter.exe BinPngConverter.cs
//    （Windows の場合: C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:winexe ...）
//
//  コマンドライン（一括変換・自動テスト用）:
//    BinPngConverter.exe --png2bin 入力画像 出力.bin [--nokey] [--size WxH]
//    BinPngConverter.exe --bin2png 入力.bin 幅 高さ 出力.png [--nokey]
//    BinPngConverter.exe --wav 入力.wav 出力.wav [--rate 44100|keep] [--keepch] [--peak -3] [--hpf 200]
//      （WAV は本体用 16bit PCM への一方向変換。既定は 44100Hz モノラル・音量そのまま）
// ============================================================================

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace BinPngConverter
{
    // ------------------------------------------------------------------------
    // 変換処理（GUI と CLI で共通）
    // ------------------------------------------------------------------------
    public static class Rgb565
    {
        public const ushort Key = 0xF81F;

        public static ushort FromRgb(int r, int g, int b)
        {
            return (ushort)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }

        public static Color ToColor(ushort v)
        {
            int r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
            // 下位ビットを補完して 0〜255 をフルに使う（白が 255 になる）
            int r = (r5 << 3) | (r5 >> 2);
            int g = (g6 << 2) | (g6 >> 4);
            int b = (b5 << 3) | (b5 >> 2);
            return Color.FromArgb(255, r, g, b);
        }

        /// <summary>画像 → RGB565 バイト列</summary>
        /// <param name="useKey">半透明より透明な画素（α&lt;128）をマゼンタ 0xF81F にする</param>
        /// <param name="resizeW">0 以下なら元サイズ</param>
        public static byte[] ImageToBin(string path, bool useKey, int resizeW, int resizeH,
                                        out int width, out int height, out int keyCollisions)
        {
            keyCollisions = 0;
            using (var src = LoadBitmapUnlocked(path))
            {
                width = resizeW > 0 ? resizeW : src.Width;
                height = resizeH > 0 ? resizeH : src.Height;
                using (var bmp = new Bitmap(width, height, PixelFormat.Format32bppArgb))
                {
                    using (var g = Graphics.FromImage(bmp))
                    {
                        g.Clear(Color.Transparent);
                        g.CompositingMode = CompositingMode.SourceCopy;
                        if (resizeW > 0 || resizeH > 0)
                        {
                            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                        }
                        else
                        {
                            g.InterpolationMode = InterpolationMode.NearestNeighbor;
                            g.PixelOffsetMode = PixelOffsetMode.Half;
                        }
                        g.DrawImage(src, new Rectangle(0, 0, width, height));
                    }
                    int[] argb = ReadArgb(bmp);
                    var outBytes = new byte[width * height * 2];
                    for (int i = 0; i < argb.Length; i++)
                    {
                        int c = argb[i];
                        int a = (c >> 24) & 0xFF, r = (c >> 16) & 0xFF, gg = (c >> 8) & 0xFF, b = c & 0xFF;
                        ushort v;
                        if (useKey && a < 128)
                        {
                            v = Key;
                        }
                        else
                        {
                            if (!useKey && a < 255)
                            {
                                // 透過を使わない場合は黒の上に合成
                                r = r * a / 255; gg = gg * a / 255; b = b * a / 255;
                            }
                            v = FromRgb(r, gg, b);
                            if (useKey && v == Key)
                            {
                                // 本物のマゼンタが透過扱いにならないよう緑を 1 段上げる
                                v = (ushort)(Key | 0x0020);
                                keyCollisions++;
                            }
                        }
                        outBytes[i * 2] = (byte)(v & 0xFF);
                        outBytes[i * 2 + 1] = (byte)(v >> 8);
                    }
                    return outBytes;
                }
            }
        }

        /// <summary>RGB565 バイト列 → PNG 用ビットマップ</summary>
        public static Bitmap BinToBitmap(byte[] data, int width, int height, bool keyToTransparent)
        {
            if ((long)width * height * 2 != data.Length)
            {
                throw new InvalidDataException(string.Format(
                    "サイズが合いません: {0}x{1}x2 = {2} バイト / ファイル {3} バイト",
                    width, height, (long)width * height * 2, data.Length));
            }
            var bmp = new Bitmap(width, height, PixelFormat.Format32bppArgb);
            var argb = new int[width * height];
            for (int i = 0; i < argb.Length; i++)
            {
                ushort v = (ushort)(data[i * 2] | (data[i * 2 + 1] << 8));
                if (keyToTransparent && v == Key)
                {
                    argb[i] = 0;  // 完全透明
                }
                else
                {
                    argb[i] = ToColor(v).ToArgb();
                }
            }
            WriteArgb(bmp, argb);
            return bmp;
        }

        static Bitmap LoadBitmapUnlocked(string path)
        {
            // Image.FromFile はファイルをロックし続けるのでメモリ経由で読む
            byte[] bytes = File.ReadAllBytes(path);
            using (var ms = new MemoryStream(bytes))
            using (var img = Image.FromStream(ms))
            {
                return new Bitmap(img);
            }
        }

        static int[] ReadArgb(Bitmap bmp)
        {
            var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
            var bd = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var result = new int[bmp.Width * bmp.Height];
            try
            {
                for (int y = 0; y < bmp.Height; y++)
                {
                    Marshal.Copy(IntPtr.Add(bd.Scan0, y * bd.Stride), result, y * bmp.Width, bmp.Width);
                }
            }
            finally { bmp.UnlockBits(bd); }
            return result;
        }

        static void WriteArgb(Bitmap bmp, int[] argb)
        {
            var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
            var bd = bmp.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                for (int y = 0; y < bmp.Height; y++)
                {
                    Marshal.Copy(argb, y * bmp.Width, IntPtr.Add(bd.Scan0, y * bd.Stride), bmp.Width);
                }
            }
            finally { bmp.UnlockBits(bd); }
        }

        // よく使うサイズ（ファイルサイズから幅・高さを推定する候補）
        static readonly int[][] KnownSizes = {
            new[] {320, 240}, new[] {320, 168}, new[] {240, 320}, new[] {100, 100}, new[] {64, 64},
            new[] {80, 112}, new[] {128, 168}, new[] {128, 128}, new[] {32, 32}, new[] {16, 16},
        };

        /// <summary>ファイル名（例: foo_64x32.bin）やサイズから幅・高さを推定。不明なら false</summary>
        public static bool GuessSize(string path, long bytes, out int w, out int h)
        {
            w = h = 0;
            if (bytes <= 0 || bytes % 2 != 0) return false;
            long px = bytes / 2;
            var m = Regex.Match(Path.GetFileNameWithoutExtension(path), @"(\d+)\s*[xX×]\s*(\d+)");
            if (m.Success)
            {
                int mw = int.Parse(m.Groups[1].Value), mh = int.Parse(m.Groups[2].Value);
                if ((long)mw * mh == px) { w = mw; h = mh; return true; }
            }
            foreach (var s in KnownSizes)
            {
                if ((long)s[0] * s[1] == px) { w = s[0]; h = s[1]; return true; }
            }
            long r = (long)Math.Round(Math.Sqrt(px));
            if (r * r == px) { w = h = (int)r; return true; }
            return false;
        }
    }

    // ------------------------------------------------------------------------
    // WAV → 本体用 WAV（16bit PCM・モノラル/ステレオ）
    //   本体 (lua_audio.cpp parseWavPcm16) が読めるのは 16bit PCM の 1/2ch のみ。
    //   play_se は data 32768 バイトまで。
    // ------------------------------------------------------------------------
    public class WavInfo
    {
        public int Tag;          // 1=PCM 3=float 6=A-law 7=μ-law（EXTENSIBLE はサブフォーマットに置換済み）
        public int Channels;
        public int Rate;
        public int Bits;
        public int FrameBytes;
        public long DataPos;
        public long DataBytes;
        public long Frames { get { return FrameBytes > 0 ? DataBytes / FrameBytes : 0; } }
        public double Seconds { get { return Rate > 0 ? (double)Frames / Rate : 0; } }

        public string Describe()
        {
            string kind = Tag == 3 ? Bits + "bit float" : Tag == 6 ? "A-law" : Tag == 7 ? "μ-law" : Bits + "bit";
            string ch = Channels == 1 ? "モノラル" : Channels == 2 ? "ステレオ" : Channels + "ch";
            return Rate + "Hz " + kind + " " + ch;
        }

        /// <summary>本体がそのまま再生できる形式か</summary>
        public bool DevicePlayable { get { return Tag == 1 && Bits == 16 && (Channels == 1 || Channels == 2); } }
    }

    public class WavOptions
    {
        public int Rate;                       // 0 = 元のまま
        public bool Mono = true;               // false = 元のまま（3ch 以上は先頭 2ch）
        public double PeakDb = double.NaN;     // NaN = 音量そのまま（クリップする場合だけ下げる）
        public double HpfHz;                   // 0 = 低音カットなし
    }

    public class WavResult
    {
        public int Rate, Channels;
        public long Frames, DataBytes;
        public double InPeakDb, GainDb;
        public bool ClipGuard;                 // クリップ防止で下げた
        public bool Dropped;                   // 3ch 以上から 2ch に減らした
        public bool Passthrough;               // 値は無変換（ヘッダ整理のみ）
    }

    public static class WavConv
    {
        public const int SeMaxBytes = 32768;

        static string Ascii(byte[] b) { return System.Text.Encoding.ASCII.GetString(b); }

        public static WavInfo ReadInfo(string path)
        {
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            using (var br = new BinaryReader(fs))
            {
                if (fs.Length < 12) throw new InvalidDataException("WAV ではありません（短すぎます）");
                string riff = Ascii(br.ReadBytes(4));
                br.ReadUInt32();
                string wave = Ascii(br.ReadBytes(4));
                if (riff == "RF64") throw new InvalidDataException("RF64（4GB 超の WAV）には対応していません");
                if (riff != "RIFF" || wave != "WAVE") throw new InvalidDataException("WAV ではありません（RIFF/WAVE ヘッダなし）");
                WavInfo info = null;
                long dataPos = -1, dataLen = 0;
                while (fs.Position + 8 <= fs.Length)
                {
                    string id = Ascii(br.ReadBytes(4));
                    long len = br.ReadUInt32();
                    long body = fs.Position;
                    if (id == "fmt ")
                    {
                        byte[] f = br.ReadBytes((int)Math.Min(len, 64));
                        if (f.Length < 16) throw new InvalidDataException("fmt チャンクが壊れています");
                        info = new WavInfo
                        {
                            Tag = BitConverter.ToUInt16(f, 0),
                            Channels = BitConverter.ToUInt16(f, 2),
                            Rate = (int)BitConverter.ToUInt32(f, 4),
                            FrameBytes = BitConverter.ToUInt16(f, 12),
                            Bits = BitConverter.ToUInt16(f, 14),
                        };
                        if (info.Tag == 0xFFFE && f.Length >= 26) info.Tag = BitConverter.ToUInt16(f, 24);
                    }
                    else if (id == "data")
                    {
                        dataPos = body;
                        dataLen = Math.Min(len, fs.Length - body);
                    }
                    long next = body + len + (len & 1);
                    if (next > fs.Length) break;
                    fs.Position = next;
                }
                if (info == null) throw new InvalidDataException("fmt チャンクがありません");
                if (dataPos < 0) throw new InvalidDataException("data チャンクがありません");
                if (info.Channels < 1 || info.Rate < 1000 || info.Rate > 384000)
                    throw new InvalidDataException("チャンネル数かサンプリング周波数が不正です");
                bool ok = (info.Tag == 1 && (info.Bits == 8 || info.Bits == 16 || info.Bits == 24 || info.Bits == 32 || info.Bits == 20 || info.Bits == 12)) ||
                          (info.Tag == 3 && (info.Bits == 32 || info.Bits == 64)) ||
                          ((info.Tag == 6 || info.Tag == 7) && info.Bits == 8);
                if (!ok) throw new InvalidDataException(string.Format("未対応の形式です（形式 {0}, {1}bit）。MP3/ADPCM 等は先に PCM WAV にしてください", info.Tag, info.Bits));
                int bps = (info.Bits + 7) / 8;
                if (info.FrameBytes < bps * info.Channels) info.FrameBytes = bps * info.Channels;
                info.DataPos = dataPos;
                info.DataBytes = dataLen - dataLen % info.FrameBytes;
                return info;
            }
        }

        /// <summary>全チャンネルを -1.0〜1.0 の float に</summary>
        public static float[][] Decode(string path, WavInfo info)
        {
            if (info.DataBytes > int.MaxValue) throw new InvalidDataException("ファイルが大きすぎます");
            byte[] d = new byte[info.DataBytes];
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                fs.Position = info.DataPos;
                int got = 0;
                while (got < d.Length)
                {
                    int n = fs.Read(d, got, d.Length - got);
                    if (n <= 0) break;
                    got += n;
                }
            }
            int ch = info.Channels, fb = info.FrameBytes, bps = (info.Bits + 7) / 8;
            long frames = info.Frames;
            var outp = new float[ch][];
            for (int c = 0; c < ch; c++) outp[c] = new float[frames];
            for (long i = 0; i < frames; i++)
            {
                int o = (int)(i * fb);
                for (int c = 0; c < ch; c++, o += bps)
                {
                    float v;
                    if (info.Tag == 3)
                    {
                        v = bps == 4 ? BitConverter.ToSingle(d, o) : (float)BitConverter.ToDouble(d, o);
                        if (float.IsNaN(v) || float.IsInfinity(v)) v = 0;
                    }
                    else if (info.Tag == 6) v = ALaw(d[o]) / 32768f;
                    else if (info.Tag == 7) v = MuLaw(d[o]) / 32768f;
                    else if (bps == 1) v = (d[o] - 128) / 128f;
                    else if (bps == 2) v = (short)(d[o] | (d[o + 1] << 8)) / 32768f;
                    else if (bps == 3) v = (((d[o] | (d[o + 1] << 8) | (d[o + 2] << 16)) << 8) >> 8) / 8388608f;
                    else v = (float)(BitConverter.ToInt32(d, o) / 2147483648.0);
                    outp[c][i] = v;
                }
            }
            return outp;
        }

        static int MuLaw(byte b)
        {
            int u = ~b & 0xFF;
            int t = (((u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4);
            return (u & 0x80) != 0 ? 0x84 - t : t - 0x84;
        }

        static int ALaw(byte b)
        {
            int a = b ^ 0x55;
            int t = (a & 0x0F) << 4;
            int seg = (a & 0x70) >> 4;
            if (seg == 0) t += 8;
            else if (seg == 1) t += 0x108;
            else t = (t + 0x108) << (seg - 1);
            return (a & 0x80) != 0 ? t : -t;
        }

        /// <summary>変換後のフレーム数（リサンプル後）</summary>
        public static long OutFrames(long frames, int fin, int fout)
        {
            return fin == fout ? frames : (long)Math.Floor((double)frames * fout / fin);
        }

        public static WavResult Convert(string inPath, string outPath, WavOptions opt)
        {
            var info = ReadInfo(inPath);
            float[][] src = Decode(inPath, info);
            var res = new WavResult();
            int fin = info.Rate;
            int fout = opt.Rate > 0 ? opt.Rate : fin;

            // チャンネル
            float[][] chs;
            bool mixed = false;
            if (opt.Mono && src.Length > 1)
            {
                var m = new float[src[0].Length];
                for (long i = 0; i < m.Length; i++)
                {
                    double s = 0;
                    for (int c = 0; c < src.Length; c++) s += src[c][i];
                    m[i] = (float)(s / src.Length);
                }
                chs = new[] { m };
                mixed = true;
            }
            else if (src.Length > 2)
            {
                chs = new[] { src[0], src[1] };
                res.Dropped = true;
            }
            else chs = src;

            double inPeak = 0;
            foreach (var c in src) foreach (var v in c) inPeak = Math.Max(inPeak, Math.Abs(v));
            res.InPeakDb = inPeak > 0 ? 20 * Math.Log10(inPeak) : double.NegativeInfinity;

            // 低音カット（2 次バターワース HPF）
            if (opt.HpfHz > 0) foreach (var c in chs) HighPass(c, fin, opt.HpfHz);

            // リサンプル
            if (fout != fin) for (int c = 0; c < chs.Length; c++) chs[c] = Resample(chs[c], fin, fout);

            // 音量
            double peak = 0;
            foreach (var c in chs) foreach (var v in c) peak = Math.Max(peak, Math.Abs(v));
            double gain = 1.0;
            if (!double.IsNaN(opt.PeakDb) && peak > 1e-9)
            {
                gain = Math.Pow(10, opt.PeakDb / 20) / peak;
            }
            else if (peak * 32768 > 32767)
            {
                gain = Math.Pow(10, -0.1 / 20) / peak;   // -0.1dB（ディザ分の余裕）
                res.ClipGuard = true;
            }
            res.GainDb = 20 * Math.Log10(gain);

            // 16bit 化（値が変わらない場合はディザなし）
            bool exact = info.Tag == 1 && info.Bits <= 16 && !mixed && fout == fin && opt.HpfHz <= 0 && gain == 1.0;
            res.Passthrough = exact;
            int nch = chs.Length;
            long frames = chs[0].Length;
            var rnd = new Random(12345);
            var pcm = new byte[frames * nch * 2];
            int p = 0;
            for (long i = 0; i < frames; i++)
            {
                for (int c = 0; c < nch; c++)
                {
                    double x = chs[c][i] * gain * 32768.0;
                    if (!exact) x += rnd.NextDouble() - rnd.NextDouble();   // TPDF ディザ ±1LSB
                    int s = (int)Math.Round(x);
                    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                    pcm[p++] = (byte)(s & 0xFF);
                    pcm[p++] = (byte)((s >> 8) & 0xFF);
                }
            }
            WritePcm16(outPath, pcm, nch, fout);
            res.Rate = fout;
            res.Channels = nch;
            res.Frames = frames;
            res.DataBytes = pcm.Length;
            return res;
        }

        static void WritePcm16(string path, byte[] pcm, int ch, int rate)
        {
            using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (var bw = new BinaryWriter(fs))
            {
                bw.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
                bw.Write((uint)(36 + pcm.Length));
                bw.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));
                bw.Write(16u);
                bw.Write((ushort)1);
                bw.Write((ushort)ch);
                bw.Write((uint)rate);
                bw.Write((uint)(rate * ch * 2));
                bw.Write((ushort)(ch * 2));
                bw.Write((ushort)16);
                bw.Write(System.Text.Encoding.ASCII.GetBytes("data"));
                bw.Write((uint)pcm.Length);
                bw.Write(pcm);
            }
        }

        static void HighPass(float[] x, int fs, double fc)
        {
            if (fc >= fs / 2.0 * 0.95) fc = fs / 2.0 * 0.95;
            double w0 = 2 * Math.PI * fc / fs, cs = Math.Cos(w0), al = Math.Sin(w0) / (2 * Math.Sqrt(0.5));
            double a0 = 1 + al;
            double b0 = (1 + cs) / 2 / a0, b1 = -(1 + cs) / a0, b2 = b0;
            double a1 = -2 * cs / a0, a2 = (1 - al) / a0;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < x.Length; i++)
            {
                double x0 = x[i];
                double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                x[i] = (float)y0;
            }
        }

        // ---- 窓付き sinc リサンプラ（Kaiser 窓 β=9、片側 16 ゼロ交差）----
        const int Zc = 16, Os = 512;
        static float[] kernel;

        static double BesselI0(double x)
        {
            double s = 1, t = 1;
            for (int k = 1; k < 50; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-12 * s) break; }
            return s;
        }

        static float[] Kernel()
        {
            if (kernel != null) return kernel;
            const double beta = 9.0;
            var k = new float[Zc * Os + 2];
            double i0b = BesselI0(beta);
            for (int i = 0; i < k.Length; i++)
            {
                double u = (double)i / Os;
                if (u >= Zc) { k[i] = 0; continue; }
                double sinc = u == 0 ? 1 : Math.Sin(Math.PI * u) / (Math.PI * u);
                double r = u / Zc;
                k[i] = (float)(sinc * BesselI0(beta * Math.Sqrt(1 - r * r)) / i0b);
            }
            return kernel = k;
        }

        public static float[] Resample(float[] x, int fin, int fout)
        {
            float[] k = Kernel();
            long nOut = OutFrames(x.Length, fin, fout);
            var y = new float[nOut];
            double c = Math.Min(1.0, (double)fout / fin) * 0.95;   // 遮断（出力ナイキストの 95%）
            double w = Zc / c;                                      // 入力サンプル単位の片側幅
            double cOs = c * Os;
            int n = x.Length;
            System.Threading.Tasks.Parallel.For(0, (int)((nOut + 4095) / 4096), blk =>
            {
                long i0 = (long)blk * 4096, i1 = Math.Min(nOut, i0 + 4096);
                for (long i = i0; i < i1; i++)
                {
                    double t = (double)(i * fin) / fout;
                    int a = (int)Math.Ceiling(t - w), b = (int)Math.Floor(t + w);
                    if (a < 0) a = 0;
                    if (b > n - 1) b = n - 1;
                    double s = 0;
                    for (int j = a; j <= b; j++)
                    {
                        double u = Math.Abs(t - j) * cOs;
                        int ui = (int)u;
                        if (ui >= Zc * Os) continue;
                        double f = u - ui;
                        s += x[j] * (k[ui] + (k[ui + 1] - k[ui]) * f);
                    }
                    y[i] = (float)(s * c);
                }
            });
            return y;
        }
    }

    // ------------------------------------------------------------------------
    // 画面
    // ------------------------------------------------------------------------
    public class MainForm : Form
    {
        static readonly string[] ImageExt = { ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff" };

        readonly ListView imageList = new ListView();
        readonly DataGridView binGrid = new DataGridView();
        readonly TextBox outDir = new TextBox();
        readonly Button browseOut = new Button();
        readonly CheckBox sameDir = new CheckBox();
        readonly CheckBox imgKey = new CheckBox();
        readonly CheckBox imgResize = new CheckBox();
        readonly NumericUpDown resizeW = new NumericUpDown();
        readonly NumericUpDown resizeH = new NumericUpDown();
        readonly CheckBox binKey = new CheckBox();
        readonly ListView wavList = new ListView();
        readonly ComboBox wavPurpose = new ComboBox();
        readonly ComboBox wavRate = new ComboBox();
        readonly ComboBox wavCh = new ComboBox();
        readonly CheckBox wavNorm = new CheckBox();
        readonly NumericUpDown wavPeak = new NumericUpDown();
        readonly CheckBox wavHpf = new CheckBox();
        readonly NumericUpDown wavHpfHz = new NumericUpDown();
        readonly Button runButton = new Button();

        static readonly int[] RateValues = { 44100, 32000, 22050, 16000, 11025, 8000, 0 };
        static readonly string[] RateNames = { "44100 Hz（BGM 推奨）", "32000 Hz", "22050 Hz", "16000 Hz", "11025 Hz（SE 向け）", "8000 Hz", "元のまま" };
        readonly TextBox log = new TextBox();

        public MainForm()
        {
            Text = "BinPngConverter — 画像 ⇔ RGB565 .bin 変換 / WAV 変換";
            Font = new Font("Meiryo UI", 9F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(900, 760);
            MinimumSize = new Size(760, 640);
            StartPosition = FormStartPosition.CenterScreen;

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 5, Padding = new Padding(8) };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 42));   // 画像 / bin ドロップ領域
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 40));   // WAV ドロップ領域
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // 出力先
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 18));   // ログ
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));      // 実行ボタン
            Controls.Add(root);

            // ---- 左上: 画像 → .bin ----
            var left = new GroupBox { Text = "画像 → .bin（PNG / JPG / BMP をここにドロップ）", Dock = DockStyle.Fill };
            imageList.Dock = DockStyle.Fill;
            imageList.View = View.Details;
            imageList.FullRowSelect = true;
            imageList.Columns.Add("ファイル", 230);
            imageList.Columns.Add("サイズ", 80);
            imageList.AllowDrop = true;
            imageList.DragEnter += OnDragEnter;
            imageList.DragDrop += (s, e) => AddFiles((string[])e.Data.GetData(DataFormats.FileDrop));
            imageList.KeyDown += (s, e) => { if (e.KeyCode == Keys.Delete) RemoveSelectedImages(); };

            var leftOpts = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
            imgKey.Text = "透明部分をマゼンタ (0xF81F) にする";
            imgKey.Checked = true;
            imgKey.AutoSize = true;
            var resizeRow = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
            imgResize.Text = "サイズ変更";
            imgResize.AutoSize = true;
            ConfigureNum(resizeW, 100);
            ConfigureNum(resizeH, 100);
            resizeW.Enabled = resizeH.Enabled = false;
            imgResize.CheckedChanged += (s, e) => resizeW.Enabled = resizeH.Enabled = imgResize.Checked;
            resizeRow.Controls.AddRange(new Control[] { imgResize, resizeW, new Label { Text = "×", AutoSize = true, Margin = new Padding(0, 6, 0, 0) }, resizeH });
            var leftButtons = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
            leftButtons.Controls.Add(MakeButton("ファイル追加...", (s, e) => BrowseImages()));
            leftButtons.Controls.Add(MakeButton("選択を削除", (s, e) => RemoveSelectedImages()));
            leftButtons.Controls.Add(MakeButton("すべて削除", (s, e) => imageList.Items.Clear()));
            leftOpts.Controls.AddRange(new Control[] { imgKey, resizeRow, leftButtons });
            left.Controls.Add(imageList);
            left.Controls.Add(leftOpts);
            root.Controls.Add(left, 0, 0);

            // ---- 右上: .bin → PNG ----
            var right = new GroupBox { Text = ".bin → PNG（.bin をここにドロップ・幅と高さを入力）", Dock = DockStyle.Fill };
            binGrid.Dock = DockStyle.Fill;
            binGrid.AllowUserToAddRows = false;
            binGrid.RowHeadersVisible = false;
            binGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            binGrid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            binGrid.BackgroundColor = SystemColors.Window;
            binGrid.Columns.Add(new DataGridViewTextBoxColumn { Name = "file", HeaderText = "ファイル", ReadOnly = true, FillWeight = 50 });
            binGrid.Columns.Add(new DataGridViewTextBoxColumn { Name = "bytes", HeaderText = "バイト", ReadOnly = true, FillWeight = 20 });
            binGrid.Columns.Add(new DataGridViewTextBoxColumn { Name = "w", HeaderText = "幅", FillWeight = 15 });
            binGrid.Columns.Add(new DataGridViewTextBoxColumn { Name = "h", HeaderText = "高さ", FillWeight = 15 });
            binGrid.Columns.Add(new DataGridViewTextBoxColumn { Name = "path", Visible = false });
            binGrid.AllowDrop = true;
            binGrid.DragEnter += OnDragEnter;
            binGrid.DragDrop += (s, e) => AddFiles((string[])e.Data.GetData(DataFormats.FileDrop));
            binGrid.CellEndEdit += (s, e) => ValidateBinRow(binGrid.Rows[e.RowIndex]);

            var rightOpts = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
            binKey.Text = "マゼンタ (0xF81F) を透明にする";
            binKey.Checked = true;
            binKey.AutoSize = true;
            var hint = new Label { Text = "幅×高さ×2 = ファイルサイズ になる組み合わせを入力\n（ファイル名の 64x32 などや、よく使うサイズから自動推定）", AutoSize = true, ForeColor = SystemColors.GrayText };
            var rightButtons = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
            rightButtons.Controls.Add(MakeButton("ファイル追加...", (s, e) => BrowseBins()));
            rightButtons.Controls.Add(MakeButton("選択を削除", (s, e) => RemoveSelectedBins()));
            rightButtons.Controls.Add(MakeButton("すべて削除", (s, e) => binGrid.Rows.Clear()));
            rightOpts.Controls.AddRange(new Control[] { binKey, hint, rightButtons });
            right.Controls.Add(binGrid);
            right.Controls.Add(rightOpts);
            root.Controls.Add(right, 1, 0);

            // ---- 中段: WAV → 本体用 WAV ----
            var wav = new GroupBox { Text = "WAV → 本体用 WAV（16bit PCM）※WAV をここにドロップ", Dock = DockStyle.Fill };
            wavList.Dock = DockStyle.Fill;
            wavList.View = View.Details;
            wavList.FullRowSelect = true;
            wavList.ShowItemToolTips = true;
            wavList.Columns.Add("ファイル", 230);
            wavList.Columns.Add("元の形式", 190);
            wavList.Columns.Add("長さ", 70);
            wavList.Columns.Add("変換後", 400);
            wavList.AllowDrop = true;
            wavList.DragEnter += OnDragEnter;
            wavList.DragDrop += (s, e) => AddFiles((string[])e.Data.GetData(DataFormats.FileDrop));
            wavList.KeyDown += (s, e) => { if (e.KeyCode == Keys.Delete) RemoveSelectedWavs(); };

            var wavOpts = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false };
            var wavRow1 = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
            wavPurpose.DropDownStyle = ComboBoxStyle.DropDownList;
            wavPurpose.Items.AddRange(new object[] { "BGM（play_wav）", "SE（play_se・32KB まで）" });
            wavPurpose.Width = 200;
            wavRate.DropDownStyle = ComboBoxStyle.DropDownList;
            wavRate.Items.AddRange(RateNames);
            wavRate.Width = 160;
            wavCh.DropDownStyle = ComboBoxStyle.DropDownList;
            wavCh.Items.AddRange(new object[] { "モノラル", "元のまま（ステレオ可）" });
            wavCh.Width = 160;
            wavRow1.Controls.AddRange(new Control[] {
                MakeLabel("用途"), wavPurpose, MakeLabel("周波数"), wavRate, MakeLabel("チャンネル"), wavCh });
            var wavRow2 = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
            wavNorm.Text = "音量をそろえる: ピーク";
            wavNorm.AutoSize = true;
            wavPeak.DecimalPlaces = 1;
            wavPeak.Increment = 0.5M;
            wavPeak.Minimum = -30;
            wavPeak.Maximum = 0;
            wavPeak.Value = -3;
            wavPeak.Width = 56;
            wavHpf.Text = "低音カット（小型スピーカーの音割れ対策）";
            wavHpf.AutoSize = true;
            wavHpf.Margin = new Padding(16, 3, 3, 3);
            wavHpfHz.Minimum = 20;
            wavHpfHz.Maximum = 2000;
            wavHpfHz.Increment = 10;
            wavHpfHz.Value = 200;
            wavHpfHz.Width = 60;
            wavRow2.Controls.AddRange(new Control[] { wavNorm, wavPeak, MakeLabel("dB"), wavHpf, wavHpfHz, MakeLabel("Hz 以下") });
            var wavButtons = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
            wavButtons.Controls.Add(MakeButton("ファイル追加...", (s, e) => BrowseWavs()));
            wavButtons.Controls.Add(MakeButton("選択を削除", (s, e) => RemoveSelectedWavs()));
            wavButtons.Controls.Add(MakeButton("すべて削除", (s, e) => wavList.Items.Clear()));
            wavOpts.Controls.AddRange(new Control[] { wavRow1, wavRow2, wavButtons });
            wav.Controls.Add(wavList);
            wav.Controls.Add(wavOpts);
            root.Controls.Add(wav, 0, 1);
            root.SetColumnSpan(wav, 2);

            wavRate.SelectedIndex = 0;
            wavCh.SelectedIndex = 0;
            wavPurpose.SelectedIndexChanged += (s, e) =>
            {
                // 用途を選ぶとおすすめの周波数にする（あとから変更可）
                wavRate.SelectedIndex = wavPurpose.SelectedIndex == 1 ? 4 : 0;
                wavCh.SelectedIndex = 0;
                RefreshWavEstimates();
            };
            wavPurpose.SelectedIndex = 0;
            wavPeak.Enabled = false;
            wavHpfHz.Enabled = false;
            wavNorm.CheckedChanged += (s, e) => wavPeak.Enabled = wavNorm.Checked;
            wavHpf.CheckedChanged += (s, e) => wavHpfHz.Enabled = wavHpf.Checked;
            wavRate.SelectedIndexChanged += (s, e) => RefreshWavEstimates();
            wavCh.SelectedIndexChanged += (s, e) => RefreshWavEstimates();

            // ---- 出力先フォルダ ----
            var outPanel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, AutoSize = true, Margin = new Padding(3, 6, 3, 3) };
            outPanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            outPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            outPanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            outPanel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            outPanel.Controls.Add(new Label { Text = "出力先フォルダ:", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 6, 4, 0) }, 0, 0);
            outDir.Dock = DockStyle.Fill;
            outDir.AllowDrop = true;
            outDir.DragEnter += OnDragEnter;
            outDir.DragDrop += (s, e) =>
            {
                var p = ((string[])e.Data.GetData(DataFormats.FileDrop)).FirstOrDefault();
                if (p != null) { outDir.Text = Directory.Exists(p) ? p : Path.GetDirectoryName(p); sameDir.Checked = false; }
            };
            outPanel.Controls.Add(outDir, 1, 0);
            browseOut.Text = "参照...";
            browseOut.AutoSize = true;
            browseOut.Click += (s, e) => BrowseOutDir();
            outPanel.Controls.Add(browseOut, 2, 0);
            sameDir.Text = "元ファイルと同じフォルダ";
            sameDir.AutoSize = true;
            sameDir.Anchor = AnchorStyles.Left;
            sameDir.CheckedChanged += (s, e) => { outDir.Enabled = browseOut.Enabled = !sameDir.Checked; };
            outPanel.Controls.Add(sameDir, 3, 0);
            root.Controls.Add(outPanel, 0, 2);
            root.SetColumnSpan(outPanel, 2);

            // ---- ログ ----
            log.Dock = DockStyle.Fill;
            log.Multiline = true;
            log.ReadOnly = true;
            log.ScrollBars = ScrollBars.Vertical;
            log.BackColor = SystemColors.Window;
            root.Controls.Add(log, 0, 3);
            root.SetColumnSpan(log, 2);

            // ---- 右下: 実行 ----
            runButton.Text = "実行";
            runButton.Font = new Font(Font.FontFamily, 11F, FontStyle.Bold);
            runButton.Size = new Size(140, 40);
            runButton.Anchor = AnchorStyles.Right;
            runButton.Click += (s, e) => Run();
            root.Controls.Add(runButton, 1, 4);

            // 出力先の初期値: 前回の設定（無ければデスクトップ）
            outDir.Text = LoadSetting() ?? Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);

            // ウィンドウのどこに落としても受け付ける（種類で振り分け）
            AllowDrop = true;
            DragEnter += OnDragEnter;
            DragDrop += (s, e) => AddFiles((string[])e.Data.GetData(DataFormats.FileDrop));

            Log("画像は左上、.bin は右上、WAV は中段にドロップしてください（どこに落としても拡張子で振り分けます）。");
        }

        static void ConfigureNum(NumericUpDown n, int value)
        {
            n.Minimum = 1;
            n.Maximum = 4096;
            n.Value = value;
            n.Width = 64;
        }

        static Label MakeLabel(string text)
        {
            return new Label { Text = text, AutoSize = true, Margin = new Padding(6, 7, 2, 0) };
        }

        static Button MakeButton(string text, EventHandler onClick)
        {
            var b = new Button { Text = text, AutoSize = true };
            b.Click += onClick;
            return b;
        }

        void OnDragEnter(object sender, DragEventArgs e)
        {
            e.Effect = e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
        }

        void Log(string s)
        {
            log.AppendText(s + Environment.NewLine);
        }

        // ドロップ・追加されたファイルを拡張子で振り分ける（フォルダは中身を展開）
        public void AddFiles(IEnumerable<string> paths)
        {
            if (paths == null) return;
            foreach (var p in paths)
            {
                if (Directory.Exists(p))
                {
                    AddFiles(Directory.GetFiles(p));
                    continue;
                }
                string ext = Path.GetExtension(p).ToLowerInvariant();
                if (ext == ".bin")
                {
                    AddBin(p);
                }
                else if (ext == ".wav" || ext == ".wave")
                {
                    AddWav(p);
                }
                else if (ImageExt.Contains(ext))
                {
                    AddImage(p);
                }
                else
                {
                    Log("対象外のファイル: " + Path.GetFileName(p));
                }
            }
        }

        void AddImage(string path)
        {
            if (imageList.Items.Cast<ListViewItem>().Any(i => (string)i.Tag == path)) return;
            string size = "?";
            try
            {
                using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read))
                using (var img = Image.FromStream(fs, false, false))
                {
                    size = img.Width + "x" + img.Height;
                }
            }
            catch (Exception ex)
            {
                Log("読み込めません: " + Path.GetFileName(path) + " (" + ex.Message + ")");
                return;
            }
            var item = new ListViewItem(new[] { Path.GetFileName(path), size }) { Tag = path, ToolTipText = path };
            imageList.Items.Add(item);
        }

        void AddBin(string path)
        {
            foreach (DataGridViewRow r in binGrid.Rows)
            {
                if ((string)r.Cells["path"].Value == path) return;
            }
            long bytes = new FileInfo(path).Length;
            int w, h;
            bool ok = Rgb565.GuessSize(path, bytes, out w, out h);
            int idx = binGrid.Rows.Add(Path.GetFileName(path), bytes.ToString(), ok ? w.ToString() : "", ok ? h.ToString() : "", path);
            ValidateBinRow(binGrid.Rows[idx]);
            if (!ok) Log(Path.GetFileName(path) + ": 幅・高さを推定できませんでした。右の表に入力してください。");
        }

        // 幅×高さ×2 がファイルサイズと一致しない行を赤くする
        bool ValidateBinRow(DataGridViewRow row)
        {
            long bytes = long.Parse((string)row.Cells["bytes"].Value);
            int w, h;
            bool ok = int.TryParse(Convert.ToString(row.Cells["w"].Value), out w) &&
                      int.TryParse(Convert.ToString(row.Cells["h"].Value), out h) &&
                      w > 0 && h > 0 && (long)w * h * 2 == bytes;
            // 幅だけ入れたら高さを自動計算
            if (!ok && int.TryParse(Convert.ToString(row.Cells["w"].Value), out w) && w > 0 &&
                string.IsNullOrWhiteSpace(Convert.ToString(row.Cells["h"].Value)) && bytes % (w * 2) == 0)
            {
                row.Cells["h"].Value = (bytes / (w * 2)).ToString();
                ok = true;
            }
            row.DefaultCellStyle.BackColor = ok ? SystemColors.Window : Color.MistyRose;
            return ok;
        }

        class WavItem
        {
            public string Path;
            public WavInfo Info;
        }

        void AddWav(string path)
        {
            if (wavList.Items.Cast<ListViewItem>().Any(i => ((WavItem)i.Tag).Path == path)) return;
            WavInfo info;
            try { info = WavConv.ReadInfo(path); }
            catch (Exception ex)
            {
                Log("読み込めません: " + Path.GetFileName(path) + " (" + ex.Message + ")");
                return;
            }
            var item = new ListViewItem(new[] {
                Path.GetFileName(path), info.Describe(), FormatTime(info.Seconds), "" })
            { Tag = new WavItem { Path = path, Info = info }, ToolTipText = path };
            wavList.Items.Add(item);
            RefreshWavEstimate(item);
        }

        static string FormatTime(double sec)
        {
            return sec < 60 ? sec.ToString("0.00") + " 秒" : ((int)sec / 60) + ":" + ((int)sec % 60).ToString("00");
        }

        WavOptions CurrentWavOptions()
        {
            return new WavOptions
            {
                Rate = RateValues[Math.Max(0, wavRate.SelectedIndex)],
                Mono = wavCh.SelectedIndex == 0,
                PeakDb = wavNorm.Checked ? (double)wavPeak.Value : double.NaN,
                HpfHz = wavHpf.Checked ? (double)wavHpfHz.Value : 0,
            };
        }

        bool IsSe { get { return wavPurpose.SelectedIndex == 1; } }

        void RefreshWavEstimates()
        {
            foreach (ListViewItem i in wavList.Items) RefreshWavEstimate(i);
        }

        // 変換後のサイズを表示。SE で 32KB を超えるものは赤
        void RefreshWavEstimate(ListViewItem item)
        {
            var info = ((WavItem)item.Tag).Info;
            var o = CurrentWavOptions();
            int rate = o.Rate > 0 ? o.Rate : info.Rate;
            int ch = o.Mono ? 1 : Math.Min(2, info.Channels);
            long bytes = WavConv.OutFrames(info.Frames, info.Rate, rate) * ch * 2;
            string text = rate + "Hz 16bit " + (ch == 1 ? "モノラル" : "ステレオ") + "  " + (bytes / 1024.0).ToString("#,0.0") + " KB";
            bool over = IsSe && bytes > WavConv.SeMaxBytes;
            if (over)
            {
                double maxSec = (double)WavConv.SeMaxBytes / (rate * ch * 2);
                text += "  ※SE は " + maxSec.ToString("0.00") + " 秒まで";
            }
            item.SubItems[3].Text = text;
            item.BackColor = over ? Color.MistyRose : SystemColors.Window;
        }

        void RemoveSelectedWavs()
        {
            foreach (ListViewItem i in wavList.SelectedItems) wavList.Items.Remove(i);
        }

        void BrowseWavs()
        {
            using (var d = new OpenFileDialog { Multiselect = true, Filter = "WAV|*.wav;*.wave|すべて|*.*" })
            {
                if (d.ShowDialog(this) == DialogResult.OK) AddFiles(d.FileNames);
            }
        }

        void RemoveSelectedImages()
        {
            foreach (ListViewItem i in imageList.SelectedItems) imageList.Items.Remove(i);
        }

        void RemoveSelectedBins()
        {
            foreach (DataGridViewRow r in binGrid.SelectedRows) binGrid.Rows.Remove(r);
        }

        void BrowseImages()
        {
            using (var d = new OpenFileDialog { Multiselect = true, Filter = "画像|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|すべて|*.*" })
            {
                if (d.ShowDialog(this) == DialogResult.OK) AddFiles(d.FileNames);
            }
        }

        void BrowseBins()
        {
            using (var d = new OpenFileDialog { Multiselect = true, Filter = "RGB565 bin|*.bin|すべて|*.*" })
            {
                if (d.ShowDialog(this) == DialogResult.OK) AddFiles(d.FileNames);
            }
        }

        void BrowseOutDir()
        {
            using (var d = new FolderBrowserDialog { Description = "出力先フォルダを選択", SelectedPath = outDir.Text })
            {
                if (d.ShowDialog(this) == DialogResult.OK) outDir.Text = d.SelectedPath;
            }
        }

        string OutputPathFor(string input, string newExt)
        {
            string dir = sameDir.Checked ? Path.GetDirectoryName(input) : outDir.Text.Trim();
            return Path.Combine(dir, Path.GetFileNameWithoutExtension(input) + newExt);
        }

        void Run()
        {
            if (imageList.Items.Count == 0 && binGrid.Rows.Count == 0 && wavList.Items.Count == 0)
            {
                Log("変換するファイルがありません。");
                return;
            }
            if (!sameDir.Checked)
            {
                string d = outDir.Text.Trim();
                if (d.Length == 0)
                {
                    MessageBox.Show(this, "出力先フォルダを選んでください。", Text);
                    return;
                }
                try { Directory.CreateDirectory(d); }
                catch (Exception ex) { MessageBox.Show(this, "出力先フォルダを作れません: " + ex.Message, Text); return; }
                SaveSetting(d);
            }

            runButton.Enabled = false;
            Cursor = Cursors.WaitCursor;
            int ok = 0, ng = 0;
            try
            {
                Log("---- 変換開始 ----");
                foreach (ListViewItem item in imageList.Items)
                {
                    string src = (string)item.Tag;
                    string dst = OutputPathFor(src, ".bin");
                    try
                    {
                        int w, h, coll;
                        byte[] data = Rgb565.ImageToBin(src, imgKey.Checked,
                            imgResize.Checked ? (int)resizeW.Value : 0, imgResize.Checked ? (int)resizeH.Value : 0,
                            out w, out h, out coll);
                        bool exists = File.Exists(dst);
                        File.WriteAllBytes(dst, data);
                        Log(string.Format("画像→bin  {0} → {1}  ({2}x{3}, {4} バイト){5}{6}",
                            Path.GetFileName(src), dst, w, h, data.Length, exists ? "  [上書き]" : "",
                            coll > 0 ? "  ※マゼンタ " + coll + " 画素を透過と区別するため微調整" : ""));
                        ok++;
                    }
                    catch (Exception ex) { Log("失敗: " + Path.GetFileName(src) + " — " + ex.Message); ng++; }
                    Application.DoEvents();
                }
                foreach (DataGridViewRow row in binGrid.Rows)
                {
                    string src = (string)row.Cells["path"].Value;
                    string dst = OutputPathFor(src, ".png");
                    try
                    {
                        if (!ValidateBinRow(row)) throw new InvalidDataException("幅×高さ×2 がファイルサイズと一致しません");
                        int w = int.Parse(Convert.ToString(row.Cells["w"].Value));
                        int h = int.Parse(Convert.ToString(row.Cells["h"].Value));
                        byte[] data = File.ReadAllBytes(src);
                        bool exists = File.Exists(dst);
                        using (var bmp = Rgb565.BinToBitmap(data, w, h, binKey.Checked))
                        {
                            bmp.Save(dst, ImageFormat.Png);
                        }
                        Log(string.Format("bin→画像  {0} → {1}  ({2}x{3}){4}", Path.GetFileName(src), dst, w, h, exists ? "  [上書き]" : ""));
                        ok++;
                    }
                    catch (Exception ex) { Log("失敗: " + Path.GetFileName(src) + " — " + ex.Message); ng++; }
                    Application.DoEvents();
                }
                var wopt = CurrentWavOptions();
                foreach (ListViewItem item in wavList.Items)
                {
                    var wi = (WavItem)item.Tag;
                    string src = wi.Path;
                    string dst = OutputPathFor(src, ".wav");
                    // 元ファイルを上書きしないよう、同じ場所なら名前を変える
                    if (string.Equals(Path.GetFullPath(dst), Path.GetFullPath(src), StringComparison.OrdinalIgnoreCase))
                    {
                        dst = Path.Combine(Path.GetDirectoryName(dst), Path.GetFileNameWithoutExtension(src) + "_conv.wav");
                    }
                    try
                    {
                        bool exists = File.Exists(dst);
                        var r = WavConv.Convert(src, dst, wopt);
                        var notes = new List<string>();
                        if (wopt.HpfHz > 0) notes.Add("低音カット " + wopt.HpfHz + "Hz");
                        if (!double.IsNaN(wopt.PeakDb) || r.ClipGuard)
                            notes.Add("音量 " + (r.GainDb >= 0 ? "+" : "") + r.GainDb.ToString("0.0") + "dB" + (r.ClipGuard ? "（音割れ防止）" : ""));
                        if (r.Dropped) notes.Add("3ch 以上のため先頭 2ch のみ");
                        if (r.Passthrough) notes.Add("音は無変換");
                        Log(string.Format("WAV       {0} → {1}  ({2}Hz {3}, {4:0.00} 秒, {5:#,0} バイト){6}{7}",
                            Path.GetFileName(src), dst, r.Rate, r.Channels == 1 ? "モノラル" : "ステレオ",
                            (double)r.Frames / r.Rate, r.DataBytes + 44,
                            notes.Count > 0 ? "  " + string.Join(" / ", notes) : "", exists ? "  [上書き]" : ""));
                        if (IsSe && r.DataBytes > WavConv.SeMaxBytes)
                            Log(string.Format("  ⚠ SE は 32KB まで（今 {0:#,0} バイト）。play_se で読めません。短くするか周波数を下げてください。", r.DataBytes));
                        ok++;
                    }
                    catch (Exception ex) { Log("失敗: " + Path.GetFileName(src) + " — " + ex.Message); ng++; }
                    Application.DoEvents();
                }
                Log(string.Format("---- 完了: 成功 {0} / 失敗 {1} ----", ok, ng));
            }
            finally
            {
                runButton.Enabled = true;
                Cursor = Cursors.Default;
            }
        }

        // 出力先を次回起動時も使えるよう exe と同じ場所に保存
        static string SettingPath
        {
            get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "BinPngConverter.last_dir.txt"); }
        }

        static string LoadSetting()
        {
            try
            {
                if (File.Exists(SettingPath))
                {
                    string s = File.ReadAllText(SettingPath).Trim();
                    if (s.Length > 0) return s;
                }
            }
            catch { }
            return null;
        }

        static void SaveSetting(string dir)
        {
            try { File.WriteAllText(SettingPath, dir); } catch { }
        }
    }

    // ------------------------------------------------------------------------
    // エントリ
    // ------------------------------------------------------------------------
    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            if (args.Length > 0 && args[0].StartsWith("--"))
            {
                return Cli(args);
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var form = new MainForm();
            // exe のアイコンにファイルをドロップして起動した場合はリストに追加
            if (args.Length > 0) form.Shown += (s, e) => form.AddFiles(args);
            Application.Run(form);
            return 0;
        }

        static int Cli(string[] a)
        {
            try
            {
                bool key = !a.Contains("--nokey");
                if (a[0] == "--png2bin" && a.Length >= 3)
                {
                    int rw = 0, rh = 0;
                    int si = Array.IndexOf(a, "--size");
                    if (si >= 0 && si + 1 < a.Length)
                    {
                        var p = a[si + 1].ToLowerInvariant().Split('x');
                        rw = int.Parse(p[0]); rh = int.Parse(p[1]);
                    }
                    int w, h, coll;
                    var data = Rgb565.ImageToBin(a[1], key, rw, rh, out w, out h, out coll);
                    File.WriteAllBytes(a[2], data);
                    Console.WriteLine("{0} -> {1} ({2}x{3}, {4} bytes, key-collisions {5})", a[1], a[2], w, h, data.Length, coll);
                    return 0;
                }
                if (a[0] == "--bin2png" && a.Length >= 5)
                {
                    var data = File.ReadAllBytes(a[1]);
                    using (var bmp = Rgb565.BinToBitmap(data, int.Parse(a[2]), int.Parse(a[3]), key))
                    {
                        bmp.Save(a[4], ImageFormat.Png);
                    }
                    Console.WriteLine("{0} -> {1}", a[1], a[4]);
                    return 0;
                }
                if (a[0] == "--wav" && a.Length >= 3)
                {
                    var o = new WavOptions();
                    int i;
                    if ((i = Array.IndexOf(a, "--rate")) >= 0) o.Rate = a[i + 1] == "keep" ? 0 : int.Parse(a[i + 1]);
                    else o.Rate = 44100;
                    if (a.Contains("--keepch")) o.Mono = false;
                    if ((i = Array.IndexOf(a, "--peak")) >= 0) o.PeakDb = double.Parse(a[i + 1], System.Globalization.CultureInfo.InvariantCulture);
                    if ((i = Array.IndexOf(a, "--hpf")) >= 0) o.HpfHz = double.Parse(a[i + 1], System.Globalization.CultureInfo.InvariantCulture);
                    var r = WavConv.Convert(a[1], a[2], o);
                    Console.WriteLine("{0} -> {1} ({2}Hz {3}ch {4} frames, gain {5:0.00}dB{6}{7})", a[1], a[2], r.Rate, r.Channels, r.Frames,
                        r.GainDb, r.ClipGuard ? " clipguard" : "", r.Passthrough ? " passthrough" : "");
                    return 0;
                }
                Console.WriteLine("usage: --png2bin in.png out.bin [--nokey] [--size WxH] | --bin2png in.bin W H out.png [--nokey]");
                Console.WriteLine("       --wav in.wav out.wav [--rate N|keep] [--keepch] [--peak dB] [--hpf Hz]");
                return 2;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("error: " + ex.Message);
                return 1;
            }
        }
    }
}
