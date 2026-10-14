using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace ItalianJobRelease
{
    sealed class Change { public string Path; public byte[] Bytes; }
    static class Installer
    {
        static string Hash(byte[] bytes) { using (var hash = SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }
        static byte[] Resource(string name)
        {
            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
            using (var memory = new MemoryStream()) { stream.CopyTo(memory); return memory.ToArray(); }
        }
        static IEnumerable<string[]> Manifest()
        {
            return System.Text.Encoding.UTF8.GetString(Resource("missions.tsv")).Split(new[] {'\n'}, StringSplitOptions.RemoveEmptyEntries).Select(line => line.Trim().Split('\t'));
        }
        static int CodeFlags(byte[] bytes)
        {
            if (bytes.Length < 256 || bytes[0] != 'M' || bytes[1] != 'Z') throw new InvalidDataException("Invalid executable.");
            int pe = BitConverter.ToInt32(bytes, 60);
            if (pe < 0 || pe + 24 >= bytes.Length || BitConverter.ToUInt32(bytes, pe) != 0x4550 || BitConverter.ToUInt16(bytes, pe + 4) != 0x14c) throw new InvalidDataException("Expected a 32-bit Windows executable.");
            int count = BitConverter.ToUInt16(bytes, pe + 6), start = pe + 24 + BitConverter.ToUInt16(bytes, pe + 20);
            if (count > 96 || start + count * 40 > bytes.Length) throw new InvalidDataException("Invalid section table.");
            var matches = new List<int>();
            for (int i = 0; i < count; i++) { int s = start + i * 40; if (System.Text.Encoding.ASCII.GetString(bytes, s, 8).TrimEnd('\0') == "_rwcseg") matches.Add(s + 36); }
            if (matches.Count != 1) throw new InvalidDataException("RenderWare code section not found.");
            return matches[0];
        }
        public static List<Change> PlanMissions(string root)
        {
            var changes = new List<Change>();
            foreach (var record in Manifest())
            {
                string path = System.IO.Path.Combine(root, "code", "exes", record[0]);
                byte[] bytes = File.ReadAllBytes(path); int call = int.Parse(record[1]);
                byte[] before = Enumerable.Range(0, 5).Select(i => Convert.ToByte(record[2].Substring(i * 2, 2), 16)).ToArray();
                if (call < 0 || call + 5 > bytes.Length || (!bytes.Skip(call).Take(5).SequenceEqual(before) && !bytes.Skip(call).Take(5).All(b => b == 0x90))) throw new InvalidDataException(record[0] + ": unsupported click-call layout.");
                var patched = (byte[])bytes.Clone(); int flags = CodeFlags(bytes);
                Buffer.BlockCopy(BitConverter.GetBytes(BitConverter.ToUInt32(bytes, flags) | 0x20000000U), 0, patched, flags, 4);
                for (int i = 0; i < 5; i++) patched[call + i] = 0x90;
                if (Hash(patched) != record[3]) throw new InvalidDataException(record[0] + ": unsupported game build or other binary modifications. No files have been changed.");
                if (!bytes.SequenceEqual(patched)) changes.Add(new Change { Path = path, Bytes = patched });
            }
            return changes;
        }
        static string SetOption(string text, string section, string key, string value)
        {
            var lines = text.Replace("\r", "").Split('\n').ToList(); string current = ""; int header = -1, found = 0;
            for (int i = 0; i < lines.Count; i++)
            {
                string trim = lines[i].Trim();
                if (trim.StartsWith("[") && trim.EndsWith("]")) { current = trim.Substring(1, trim.Length - 2); if (current.Equals(section, StringComparison.OrdinalIgnoreCase)) header = i; }
                if (current.Equals(section, StringComparison.OrdinalIgnoreCase) && Regex.IsMatch(trim, "^" + Regex.Escape(key) + @"\s*=", RegexOptions.IgnoreCase))
                {
                    string previous = trim.Substring(trim.IndexOf('=') + 1).Trim();
                    if (key == "LoadCustomDllPath" && previous.Length != 0 && !previous.Equals(value, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("A custom DLL is already configured. Add TIJHudFix.asi to your loader manually instead of overwriting it.");
                    lines[i] = key + " = " + value; found++;
                }
            }
            if (found > 1) throw new InvalidDataException("Duplicate " + key + " settings; correct the INI before installing.");
            if (found == 0) { if (header < 0) { lines.Add("[" + section + "]"); lines.Add(key + " = " + value); } else lines.Insert(header + 1, key + " = " + value); }
            return string.Join("\r\n", lines);
        }
        public static int Apply(string root)
        {
            ItalianJobOptions.GameSettings.EnsureGameClosed();
            if (!File.Exists(System.IO.Path.Combine(root, "TIJ.exe"))) throw new FileNotFoundException("Place TIJ Fixes.exe beside the game's TIJ.exe.");
            var changes = PlanMissions(root);
            foreach (string folder in new[] { root, System.IO.Path.Combine(root, "code", "exes") })
            {
                foreach (string file in new[] { "d3d8.dll", "dxwrapper.dll" })
                {
                    byte[] bundled = Resource(file); string target = System.IO.Path.Combine(folder, file);
                    if (File.Exists(target) && !File.ReadAllBytes(target).SequenceEqual(bundled)) throw new InvalidOperationException(target + " differs from the included DxWrapper. Keep your wrapper and install the fixes manually; it will not be overwritten.");
                    changes.Add(new Change { Path = target, Bytes = bundled });
                }
                string iniPath = System.IO.Path.Combine(folder, "dxwrapper.ini"); string ini = File.Exists(iniPath) ? File.ReadAllText(iniPath) : "";
                ini = SetOption(ini, "Compatibility", "D3d8to9", "1");
                if (!folder.Equals(root, StringComparison.OrdinalIgnoreCase)) ini = SetOption(ini, "General", "LoadCustomDllPath", "TIJHudFix.asi");
                changes.Add(new Change { Path = iniPath, Bytes = System.Text.Encoding.UTF8.GetBytes(ini) });
            }
            string asi = System.IO.Path.Combine(root, "code", "exes", "TIJHudFix.asi");
            changes.Add(new Change { Path = asi, Bytes = Resource("TIJHudFix.asi") });
            // Every game/version/configuration check completes before any file is written.
            foreach (var change in changes) if (File.Exists(change.Path)) using (var test = new FileStream(change.Path, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
            foreach (var change in changes)
            {
                using (var stream = new FileStream(change.Path, FileMode.OpenOrCreate, FileAccess.Write, FileShare.None))
                { stream.Write(change.Bytes, 0, change.Bytes.Length); stream.SetLength(change.Bytes.Length); stream.Flush(true); }
                if (!File.ReadAllBytes(change.Path).SequenceEqual(change.Bytes)) throw new IOException("Write verification failed: " + change.Path);
            }
            return changes.Count;
        }
        [STAThread] static int Main(string[] args)
        {
            try
            {
                if (args.Length == 2 && args[0] == "--apply") { Apply(System.IO.Path.GetFullPath(args[1])); return 0; }
                if (args.Length == 2 && args[0] == "--verify-missions") { PlanMissions(System.IO.Path.GetFullPath(args[1])); return 0; }
                Application.EnableVisualStyles();
                if (MessageBox.Show("Install the mission-loading, Remix input and HUD placement fixes?\n\nExisting game saves and Remix settings are preserved. No backups are created.", "The Italian Job Fixes", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return 0;
                Apply(AppDomain.CurrentDomain.BaseDirectory);
                MessageBox.Show("Fixes installed. Use TIJ Options.exe to set the resolution, then start TIJ.exe.", "The Italian Job Fixes"); return 0;
            }
            catch (Exception error)
            {
                if (args.Length > 0) File.WriteAllText(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "install-error.txt"), error.ToString());
                else MessageBox.Show(error.Message, "The Italian Job Fixes", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }
}
