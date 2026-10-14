$disk = 3                                  # the card: check with Get-Disk!
$file = "lfblocks.fb4"    # fill in the actual path

# 1. Launch Windows PowerShell in Administrator mode.
# 2. Enter `Get-Disk` to list the disk numbers. Verify that $disk matches.
# 3. Enter `powershell -ExecutionPolicy Bypass` to allow the script to run.

$part = Get-Partition -DiskNumber $disk | Where-Object MbrType -eq 0xDA | Select-Object -First 1
if (-not $part) { throw "disk $disk has no partition of type DA" }
$data = [System.IO.File]::ReadAllBytes($file)
if ($data.Length -gt $part.Size) { throw "$file is larger than the partition" }
if ($data.Length % 512) { throw "$file isn't whole sectors" }

# Windows PowerShell's FileStream won't open \\.\ devices by name, so
# open the disk with CreateFile and give FileStream the handle.
Add-Type -Namespace LF -Name Disk -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFile(
    string name, int access, int share, IntPtr security, int creation, int flags, IntPtr template);
'@
# GENERIC_READ | GENERIC_WRITE, shared, OPEN_EXISTING
$h = [LF.Disk]::CreateFile("\\.\PhysicalDrive$disk", -1073741824, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
if ($h.IsInvalid) { throw "can't open disk ${disk}: run PowerShell as administrator" }
$s = New-Object System.IO.FileStream($h, [System.IO.FileAccess]::ReadWrite)
$s.Seek($part.Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
$s.Write($data, 0, $data.Length)
$s.Flush()

# Check: read the blocks back and compare
$back = New-Object byte[] $data.Length
$s.Seek($part.Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
$n = 0
while ($n -lt $back.Length) { $n += $s.Read($back, $n, $back.Length - $n) }
$s.Close()
if ([Convert]::ToBase64String($back) -eq [Convert]::ToBase64String($data)) { "copied" } else { "the card differs from $file" }
