# Test client for the mod's named pipe (no build needed, plain PowerShell).
#   .\pipe-test.ps1                      connect, run a short self-test, print every reply
#   .\pipe-test.ps1 -Watch               connect and just print what the mod sends (Ctrl+C to stop)
#   .\pipe-test.ps1 -Send '{"cmd":"action","name":"toggle:low-latency"}'
param([switch]$Watch, [string]$Send = "", [string]$Name = "abhyu-input-buffer-testing")

$pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $Name, [System.IO.Pipes.PipeDirection]::InOut)
try { $pipe.Connect(3000) } catch { Write-Host "Could not connect: is Geometry Dash running with the mod loaded?"; exit 1 }
$reader = New-Object System.IO.StreamReader($pipe)
$writer = New-Object System.IO.StreamWriter($pipe)
$writer.AutoFlush = $true
Write-Host "Connected to \\.\pipe\$Name"

function Send-Line([string]$line) { Write-Host ">> $line" -ForegroundColor Cyan; $writer.WriteLine($line) }

$script:task = $reader.ReadLineAsync()
function Pump([int]$ms) {
    $end = [DateTime]::Now.AddMilliseconds($ms)
    while ([DateTime]::Now -lt $end) {
        if ($script:task.Wait(100)) {
            $line = $script:task.Result
            if ($null -eq $line) { Write-Host "Mod closed the pipe."; exit 0 }
            $short = if ($line.Length -gt 220) { $line.Substring(0, 220) + " ..." } else { $line }
            Write-Host "<< $short"
            $script:task = $reader.ReadLineAsync()
        }
    }
}

if ($Send -ne "") { Send-Line $Send; Pump 1500; exit 0 }
if ($Watch) { while ($true) { Pump 1000 } }

Pump 1500                                                  # hello, schema, config, first state
Send-Line '{"cmd":"ping","id":1}'; Pump 500                # expect pong
Send-Line '{"cmd":"set","key":"tps","value":5,"id":2}'; Pump 500            # expect ok:false (range 240..1000)
Send-Line '{"cmd":"set","key":"nope","value":true,"id":3}'; Pump 500        # expect ok:false (unknown key)
Send-Line '{"cmd":"set","key":"bind.export","value":"F6"}'; Pump 500        # expect ok:false (F6 already used)
Send-Line '{"cmd":"action","name":"toggle:low-latency"}'; Pump 800          # flips low-latency, config comes back
Send-Line '{"cmd":"action","name":"toggle:low-latency"}'; Pump 800          # flips it back
Send-Line '{"cmd":"preset","name":"balanced"}'; Pump 800
Write-Host "Self-test done. Closing (the mod keeps running and accepts the next client)."
$pipe.Dispose()
