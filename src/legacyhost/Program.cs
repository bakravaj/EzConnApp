using System;
using Schnell.TeleAssistenza.Protocollo;

// Private child-process IPC: one ASCII request/response per line on stdin/stdout.
// This process never opens a network or serial connection.
static class Program {
    static byte[] Unhex(string s) {
        if (s == "-") return new byte[0];
        if (s.Length % 2 != 0 || s.Length > 2097152) throw new FormatException("Invalid hex length");
        byte[] b = new byte[s.Length / 2];
        for (int i = 0; i < b.Length; i++) b[i] = Convert.ToByte(s.Substring(i*2,2),16);
        return b;
    }
    static string Hex(byte[] b) { return b.Length == 0 ? "-" : BitConverter.ToString(b).Replace("-", ""); }
    static int Width(byte t) {
        switch(t) { case 80: return 1; case 67: case 84: return 2; case 66: case 88: return 4; default: throw new FormatException("Unsupported packet type (compression disabled)"); }
    }
    static string Request(string line) {
        string[] p = line.Split(' ');
        if (p.Length == 1 && p[0] == "hello") return "OK legacyhost-1 " + typeof(formatoComando).FullName;
        if (p.Length == 2 && p[0] == "auth") return "OK " + codifica.getCodifica(p[1]);
        if (p.Length == 5 && p[0] == "encode") {
            byte type = Convert.ToByte(p[1]); int w = Width(type);
            byte[] data = Unhex(p[4]);
            if (data.Length > 1048576 || (w == 1 && data.Length > 251) || (w == 2 && data.Length > 65531)) throw new FormatException("Payload too large");
            var packet = new formatoComando(type,0,0,65,66,Convert.ToByte(p[2]),Convert.ToByte(p[3]),new byte[0]);
            packet.SetDati(data);
            return "OK " + Hex(packet.GetPacchettoInByte());
        }
        if (p.Length == 2 && p[0] == "parse") {
            byte[] b = Unhex(p[1]);
            if (b.Length < 7) throw new FormatException("Truncated packet");
            int w = Width(b[0]); uint len = 0;
            for (int i=0;i<w;i++) len |= (uint)b[i+1] << (8*i);
            if (len < 4 || len > 1048580 || (long)len+w+2 != b.Length) throw new FormatException("Invalid length");
            byte lrc=0; for(int i=1;i<b.Length-1;i++) lrc ^= b[i];
            if (lrc != b[b.Length-1]) throw new FormatException("Invalid LRC");
            if (b[w+4] == 69 && len == 4) throw new FormatException("Empty error payload");
            var packet = new formatoComando(); packet.SetPacchetto(b);
            return "OK " + packet.GetTipoPacchetto()+" "+b[w+1]+" "+b[w+2]+" "+packet.GetComando()+" "+b[w+4]+" "+Hex(packet.GetDati());
        }
        throw new FormatException("Unknown request");
    }
    static int Main() {
        // Keep diagnostics from legacy code away from the IPC stream.
        var output = Console.Out; Console.SetOut(Console.Error);
        var input = Console.OpenStandardInput();
        while (true) {
            var line = new System.Text.StringBuilder(); int c;
            while ((c=input.ReadByte()) != -1 && c != '\n') {
                if (line.Length >= 2097200) { output.WriteLine("ERR Request too large"); output.Flush(); return 2; }
                if (c != '\r') line.Append((char)c);
            }
            if (c == -1 && line.Length == 0) return 0;
            try { output.WriteLine(Request(line.ToString())); }
            catch (Exception e) { output.WriteLine("ERR " + e.Message.Replace('\r',' ').Replace('\n',' ')); }
            output.Flush();
        }
    }
}
