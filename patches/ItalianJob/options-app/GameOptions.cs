using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace ItalianJobOptions
{
    static class Program
    {
        [STAThread] static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                if (args.Length > 0 && args[0] == "--self-test") { Tests.Run(args[1], args[2]); return 0; }
                var form = new OptionsForm(AppDomain.CurrentDomain.BaseDirectory);
                if (args.Length > 0 && args[0] == "--preview")
                {
                    form.Show(); form.Refresh(); Application.DoEvents();
                    using (var bitmap = new Bitmap(form.Width, form.Height)) { form.DrawToBitmap(bitmap, form.ClientRectangle); bitmap.Save(args[1]); }
                    form.Close(); return 0;
                }
                Application.Run(form); return 0;
            }
            catch (Exception error)
            {
                if (args.Length > 2 && args[0] == "--self-test") File.WriteAllText(args[2], error.ToString());
                else MessageBox.Show(error.Message, "The Italian Job Options", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }

    sealed class Mode
    {
        public int Width, Height;
        public Mode(int width, int height) { Width = width; Height = height; }
        public override string ToString()
        {
            int divisor = Gcd(Width, Height);
            return Width + " × " + Height + "    (" + Width / divisor + ":" + Height / divisor + ")";
        }
        static int Gcd(int a, int b) { while (b != 0) { int r = a % b; a = b; b = r; } return a; }
    }

    static class DisplayModes
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct DevMode
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Device;
            public ushort SpecVersion, DriverVersion, Size, DriverExtra;
            public uint Fields;
            public int PositionX, PositionY;
            public uint Orientation, FixedOutput;
            public short Color, Duplex, YResolution, TTOption, Collate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string FormName;
            public ushort LogPixels;
            public uint BitsPerPel, Width, Height, DisplayFlags, Frequency, ICMMethod, ICMIntent, MediaType, DitherType, Reserved1, Reserved2, PanningWidth, PanningHeight;
        }
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDisplaySettings(string device, int index, ref DevMode mode);
        public static List<Mode> Get()
        {
            var result = new List<Mode>();
            for (int i = 0; ; i++)
            {
                var mode = new DevMode(); mode.Size = (ushort)Marshal.SizeOf(typeof(DevMode));
                if (!EnumDisplaySettings(null, i, ref mode)) break;
                if (mode.BitsPerPel == 32 && mode.Width >= 640 && mode.Height >= 480 && !result.Any(m => m.Width == mode.Width && m.Height == mode.Height))
                    result.Add(new Mode((int)mode.Width, (int)mode.Height));
            }
            return result.OrderBy(m => m.Width).ThenBy(m => m.Height).ToList();
        }
    }

    sealed class GameSettings
    {
        public readonly string Root;
        static readonly byte[] Anchor = { 0x75, 0x2c, 0x81, 0x7d, 0xd0 };
        public GameSettings(string root) { Root = Path.GetFullPath(root); }
        string Exe { get { return Path.Combine(Root, "TIJ.exe"); } }
        string Save { get { return Path.Combine(Root, "res_data.dat"); } }
        public static List<int> FindChecks(byte[] bytes)
        {
            var candidates = new List<int>();
            for (int i = 7; i < bytes.Length - 20; i++)
            {
                if (!Anchor.Select((b, n) => bytes[i + n] == b).All(b => b)) continue;
                if (bytes[i - 7] != 0x81 || bytes[i - 6] != 0x7d || bytes[i - 5] != 0xcc) continue;
                if (bytes[i + 9] != 0x75 || bytes[i + 10] != 0x23 || bytes[i + 11] != 0x83 || bytes[i + 12] != 0x7d || bytes[i + 13] != 0xc8) continue;
                if (bytes[i + 14] != 0x10 && bytes[i + 14] != 0x20) continue;
                candidates.Add(i - 4);
            }
            // These builds have six contiguous display checks, separated by 0x35 bytes.
            var groups = candidates.Where(c => Enumerable.Range(0, 6).All(n => candidates.Contains(c + n * 0x35))).ToList();
            if (groups.Count != 1) throw new InvalidDataException("This TIJ.exe has an unrecognized display-mode layout. Nothing was changed.");
            int first = groups[0];
            if (BitConverter.ToInt32(bytes, first + 2 * 0x35) != 800 || BitConverter.ToInt32(bytes, first + 2 * 0x35 + 9) != 600 ||
                BitConverter.ToInt32(bytes, first + 4 * 0x35) != 1024 || BitConverter.ToInt32(bytes, first + 4 * 0x35 + 9) != 768 ||
                BitConverter.ToInt32(bytes, first) != BitConverter.ToInt32(bytes, first + 0x35) ||
                BitConverter.ToInt32(bytes, first + 9) != BitConverter.ToInt32(bytes, first + 0x35 + 9))
                throw new InvalidDataException("The resolution slots do not match this supported game build. Nothing was changed.");
            return new List<int> { first, first + 0x35 };
        }
        public Mode Read()
        {
            byte[] exe = File.ReadAllBytes(Exe); var checks = FindChecks(exe);
            byte[] save = ReadSave(); int slot = BitConverter.ToUInt16(save, 0x14c);
            if (slot > 5) throw new InvalidDataException("The saved display-mode choice is invalid.");
            if (slot == 1 || slot == 4) return new Mode(800, 600);
            if (slot == 2 || slot == 5) return new Mode(1024, 768);
            return new Mode(BitConverter.ToInt32(exe, checks[0]), BitConverter.ToInt32(exe, checks[0] + 9));
        }
        byte[] ReadSave()
        {
            byte[] bytes = File.ReadAllBytes(Save);
            if (bytes.Length != 352) throw new InvalidDataException("Expected the game's 352-byte res_data.dat. Nothing was changed.");
            return bytes;
        }
        public static void EnsureGameClosed()
        {
            foreach (var process in Process.GetProcesses())
            {
                using (process)
                {
                    string name = process.ProcessName;
                    if (name.Equals("TIJ", StringComparison.OrdinalIgnoreCase) ||
                        (name.Length == 6 && name.StartsWith("stg", StringComparison.OrdinalIgnoreCase) && name.Substring(3).All(char.IsDigit)))
                        throw new InvalidOperationException("Close The Italian Job and its mission windows before applying display settings.");
                }
            }
        }
        public void Apply(Mode mode)
        {
            EnsureGameClosed();
            if (mode.Width < 640 || mode.Height < 480 || mode.Width > 16384 || mode.Height > 16384) throw new ArgumentOutOfRangeException("mode");
            byte[] originalExe = File.ReadAllBytes(Exe), originalSave = ReadSave(); var checks = FindChecks(originalExe);
            byte[] exe = (byte[])originalExe.Clone(), save = (byte[])originalSave.Clone();
            foreach (int offset in checks) { Buffer.BlockCopy(BitConverter.GetBytes(mode.Width), 0, exe, offset, 4); Buffer.BlockCopy(BitConverter.GetBytes(mode.Height), 0, exe, offset + 9, 4); }
            save[0x14c] = 3; save[0x14d] = 0;
            try
            {
                File.WriteAllBytes(Exe, exe); File.WriteAllBytes(Save, save);
                if (!File.ReadAllBytes(Exe).SequenceEqual(exe) || !File.ReadAllBytes(Save).SequenceEqual(save)) throw new IOException("Settings verification failed.");
            }
            catch { File.WriteAllBytes(Exe, originalExe); File.WriteAllBytes(Save, originalSave); throw; }
        }
        public void Launch()
        {
            EnsureGameClosed();
            Process.Start(new ProcessStartInfo(Exe) { WorkingDirectory = Root, UseShellExecute = true });
        }
    }

    sealed class OptionsForm : Form
    {
        readonly GameSettings settings;
        readonly ComboBox resolution = new ComboBox();
        readonly Label detected = new Label();
        readonly Color foreground = Color.FromArgb(231, 234, 239);
        public OptionsForm(string root)
        {
            settings = new GameSettings(root); Text = "The Italian Job — Display Options";
            ClientSize = new Size(420, 195); MinimumSize = MaximumSize = Size; FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(24, 28, 35);
            ForeColor = foreground; Font = new Font("Segoe UI", 10f); AutoScaleMode = AutoScaleMode.Dpi;
            AddLabel("Resolution", 24, 21, 372, 26, 11, FontStyle.Bold);
            resolution.SetBounds(24, 55, 372, 34); resolution.DropDownStyle = ComboBoxStyle.DropDownList;
            resolution.BackColor = Color.FromArgb(40, 46, 56); resolution.ForeColor = foreground; resolution.FlatStyle = FlatStyle.Flat;
            var modes = DisplayModes.Get(); Mode current = settings.Read();
            foreach (var mode in modes) resolution.Items.Add(mode);
            if (modes.Count == 0) throw new InvalidOperationException("Windows did not report any supported 32-bit display modes.");
            int index = modes.FindIndex(m => m.Width == current.Width && m.Height == current.Height);
            resolution.SelectedIndex = index >= 0 ? index : modes.FindIndex(m => m.Width == Screen.PrimaryScreen.Bounds.Width && m.Height == Screen.PrimaryScreen.Bounds.Height);
            if (resolution.SelectedIndex < 0) resolution.SelectedIndex = modes.Count - 1;
            Controls.Add(resolution);
            detected.Text = "Detected resolution: " + current.Width + " x " + current.Height;
            detected.SetBounds(24, 98, 372, 26); detected.Font = new Font("Segoe UI", 9); detected.ForeColor = foreground; Controls.Add(detected);
            var apply = Button("Apply", 24, 142, 120); apply.Click += delegate { Apply(false); };
            var launch = Button("Apply & Play", 158, 142, 162); launch.Click += delegate { Apply(true); };

        }
        void AddLabel(string text, int x, int y, int width, int height, float size, FontStyle style)
        {
            var label = new Label { Text = text, Font = new Font("Segoe UI", size, style), ForeColor = foreground }; label.SetBounds(x, y, width, height); Controls.Add(label);
        }
        Button Button(string text, int x, int y, int width)
        {
            var button = new Button { Text = text, FlatStyle = FlatStyle.Flat, BackColor = Color.FromArgb(44, 63, 79), ForeColor = foreground, Cursor = Cursors.Hand }; button.FlatAppearance.BorderColor = Color.FromArgb(68, 88, 105); button.SetBounds(x, y, width, 38); Controls.Add(button); return button;
        }
        void Apply(bool launch)
        {
            try
            {
                var mode = (Mode)resolution.SelectedItem; settings.Apply(mode); detected.Text = "Detected resolution: " + mode.Width + " x " + mode.Height;
                if (launch) { settings.Launch(); Close(); }
            }
            catch (Exception error) { MessageBox.Show(this, error.Message, "Display options", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
    }

    static class Tests
    {
        public static void Run(string gameRoot, string report)
        {
            string temp = Path.Combine(Path.GetTempPath(), "ItalianJobOptions-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(temp);
            byte[] originalExe = File.ReadAllBytes(Path.Combine(gameRoot, "TIJ.exe")), originalSave = File.ReadAllBytes(Path.Combine(gameRoot, "res_data.dat"));
            File.WriteAllBytes(Path.Combine(temp, "TIJ.exe"), originalExe); File.WriteAllBytes(Path.Combine(temp, "res_data.dat"), originalSave);
            var engine = new GameSettings(temp); var checks = GameSettings.FindChecks(originalExe);
            engine.Apply(new Mode(1920, 1080)); byte[] patched = File.ReadAllBytes(Path.Combine(temp, "TIJ.exe")), save = File.ReadAllBytes(Path.Combine(temp, "res_data.dat"));
            for (int i = 0; i < originalExe.Length; i++) if (originalExe[i] != patched[i] && !checks.Any(o => (i >= o && i < o + 4) || (i >= o + 9 && i < o + 13))) throw new Exception("Unexpected executable modification.");
            for (int i = 0; i < save.Length; i++) if (i != 0x14c && i != 0x14d && save[i] != originalSave[i]) throw new Exception("Save progress changed.");
            if (engine.Read().Width != 1920 || engine.Read().Height != 1080) throw new Exception("Readback failed.");
            engine.Apply(new Mode(2560, 1440)); if (engine.Read().Width != 2560) throw new Exception("Reapplying settings failed.");
            engine.Apply(new Mode(BitConverter.ToInt32(originalExe, checks[0]), BitConverter.ToInt32(originalExe, checks[0] + 9))); if (!File.ReadAllBytes(Path.Combine(temp, "TIJ.exe")).SequenceEqual(originalExe)) throw new Exception("Executable restore failed.");
            byte[] invalid = (byte[])originalExe.Clone(); invalid[checks[0] - 1] ^= 1;
            bool rejected = false; try { GameSettings.FindChecks(invalid); } catch (InvalidDataException) { rejected = true; }
            if (!rejected) throw new Exception("Unrecognized layout was accepted.");
            if (Directory.Exists(Path.Combine(temp, "options-backups"))) throw new Exception("Unexpected backup folder.");
            File.WriteAllText(report, "PASS: native resolution read/apply/reapply/revert; only guarded constants changed; save progress preserved; unsupported binary rejected; no backups created.\nFixture: " + temp);
        }
    }
}
