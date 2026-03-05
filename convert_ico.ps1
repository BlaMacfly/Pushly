$ErrorActionPreference = "Stop"

$pngPath = "C:\Users\Arkin\.gemini\antigravity\brain\017ce6fd-330d-4bfa-9c69-84ca1eaa39d1\pushly_logo_transparent_1772730847699.png"
$icoPath = ".\Pushly.ico"

# C# code to convert PNG to ICO properly (1 icon image)
$csharp = @"
using System;
using System.Drawing;
using System.IO;

public class IconTool {
    public static void ConvertToIco(string pngPath, string icoPath) {
        using (Bitmap bmp = new Bitmap(pngPath)) {
            // Resize to 256x256 (typical max for ICO)
            using (Bitmap resized = new Bitmap(bmp, new Size(256, 256))) {
                using (FileStream fs = new FileStream(icoPath, FileMode.Create)) {
                    using (MemoryStream ms = new MemoryStream()) {
                        resized.Save(ms, System.Drawing.Imaging.ImageFormat.Png);
                        byte[] pngBytes = ms.ToArray();

                        // Write ICONDIR
                        fs.Write(new byte[] { 0, 0 }, 0, 2); // idReserved
                        fs.Write(new byte[] { 1, 0 }, 0, 2); // idType (1 = ICO)
                        fs.Write(new byte[] { 1, 0 }, 0, 2); // idCount (1 image)

                        // Write ICONDIRENTRY
                        fs.WriteByte(0); // bWidth (0 = 256)
                        fs.WriteByte(0); // bHeight (0 = 256)
                        fs.WriteByte(0); // bColorCount
                        fs.WriteByte(0); // bReserved
                        fs.Write(new byte[] { 1, 0 }, 0, 2); // wPlanes
                        fs.Write(new byte[] { 32, 0 }, 0, 2); // wBitCount
                        
                        byte[] sizeBytes = BitConverter.GetBytes((uint)pngBytes.Length);
                        fs.Write(sizeBytes, 0, 4); // dwBytesInRes
                        
                        byte[] offsetBytes = BitConverter.GetBytes((uint)(6 + 16));
                        fs.Write(offsetBytes, 0, 4); // dwImageOffset

                        // Write PNG data
                        fs.Write(pngBytes, 0, pngBytes.Length);
                    }
                }
            }
        }
    }
}
"@

Add-Type -TypeDefinition $csharp -ReferencedAssemblies System.Drawing
[IconTool]::ConvertToIco($pngPath, $icoPath)

Write-Host "Created $icoPath successfully."
