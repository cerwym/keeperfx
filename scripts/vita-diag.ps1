#!/usr/bin/env pwsh
# Vita Diagnostics: test vdbtcp ports + FTP + GDB
param(
    [string]$VitaIP = $(if ($env:VITA_IP) { $env:VITA_IP } else { "192.168.0.66" })
)

$ErrorActionPreference = "Stop"

function Write-Section($title) {
    Write-Host "`n=== $title ===" -ForegroundColor Cyan
}

function Test-Port($ip, $port, $timeoutMs = 3000) {
    $tcp = New-Object System.Net.Sockets.TcpClient
    try {
        $ar = $tcp.BeginConnect($ip, $port, $null, $null)
        if ($ar.AsyncWaitHandle.WaitOne($timeoutMs)) {
            $tcp.EndConnect($ar)
            return $true
        }
        return $false
    } catch {
        return $false
    } finally {
        $tcp.Dispose()
    }
}

# --- 1. Port Scan ---
Write-Section "Port Scan"
$ports = @(
    @{Port=1337; Desc="VitaShell FTP"},
    @{Port=1338; Desc="vdbtcp command"},
    @{Port=1339; Desc="vdbtcp FTP"},
    @{Port=31337; Desc="vdbtcp GDB"}
)
foreach ($p in $ports) {
    $result = Test-Port $VitaIP $p.Port 3000
    $status = if ($result) { "OPEN" } else { "CLOSED/TIMEOUT" }
    $color = if ($result) { "Green" } else { "Red" }
    Write-Host ("  {0,-5} ({1,-15}) : {2}" -f $p.Port, $p.Desc, $status) -ForegroundColor $color
}

# --- 2. Command Port Test ---
Write-Section "Command Port (1338) - screen on"
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.Connect($VitaIP, 1338)
    $s = $tcp.GetStream()
    $s.ReadTimeout = 5000
    $s.WriteTimeout = 3000
    $cmd = [Text.Encoding]::ASCII.GetBytes("screen on`n")
    $s.Write($cmd, 0, $cmd.Length)
    $s.Flush()
    Start-Sleep -Milliseconds 1000
    $buf = New-Object byte[] 1024
    try {
        $n = $s.Read($buf, 0, 1024)
        $resp = [Text.Encoding]::ASCII.GetString($buf, 0, $n)
        Write-Host "  Response ($n bytes): $resp" -ForegroundColor Green
    } catch {
        Write-Host "  No response (timeout)" -ForegroundColor Yellow
    }
    $tcp.Close()
} catch {
    Write-Host "  ERROR: $_" -ForegroundColor Red
}

# --- 3. vdbtcp FTP Test (port 1339) ---
Write-Section "vdbtcp FTP (1339)"
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.Connect($VitaIP, 1339)
    $s = $tcp.GetStream()
    $s.ReadTimeout = 5000
    $reader = New-Object System.IO.StreamReader($s)
    $writer = New-Object System.IO.StreamWriter($s)
    $writer.AutoFlush = $true

    # Read banner
    $banner = $reader.ReadLine()
    Write-Host "  Banner: $banner" -ForegroundColor Green

    if ($banner -match "^220") {
        # Login (usually anonymous)
        $writer.WriteLine("USER anonymous")
        $resp = $reader.ReadLine()
        Write-Host "  USER: $resp"

        $writer.WriteLine("PASS x")
        $resp = $reader.ReadLine()
        Write-Host "  PASS: $resp"

        # List ur0:/tai/
        $writer.WriteLine("CWD ur0:/tai")
        $resp = $reader.ReadLine()
        Write-Host "  CWD ur0:/tai: $resp"

        # Use PASV for listing
        $writer.WriteLine("PASV")
        $pasvResp = $reader.ReadLine()
        Write-Host "  PASV: $pasvResp"

        if ($pasvResp -match "\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)") {
            $dataPort = [int]$Matches[5] * 256 + [int]$Matches[6]
            $dataIP = "$($Matches[1]).$($Matches[2]).$($Matches[3]).$($Matches[4])"
            Write-Host "  Data endpoint: ${dataIP}:${dataPort}"

            $dataTcp = New-Object System.Net.Sockets.TcpClient
            $dataTcp.Connect($dataIP, $dataPort)
            $dataStream = $dataTcp.GetStream()
            $dataStream.ReadTimeout = 5000

            $writer.WriteLine("LIST")
            $listResp = $reader.ReadLine()
            Write-Host "  LIST: $listResp"

            Start-Sleep -Milliseconds 1000
            $dataBuf = New-Object byte[] 8192
            try {
                $total = ""
                while ($dataStream.DataAvailable) {
                    $n = $dataStream.Read($dataBuf, 0, 8192)
                    $total += [Text.Encoding]::ASCII.GetString($dataBuf, 0, $n)
                }
                if ($total.Length -gt 0) {
                    Write-Host "  --- tai/ listing ---" -ForegroundColor Yellow
                    $total -split "`n" | ForEach-Object { Write-Host "    $_" }
                } else {
                    # Wait more and try reading
                    Start-Sleep -Milliseconds 2000
                    $n = $dataStream.Read($dataBuf, 0, 8192)
                    $total = [Text.Encoding]::ASCII.GetString($dataBuf, 0, $n)
                    Write-Host "  --- tai/ listing ---" -ForegroundColor Yellow
                    $total -split "`n" | ForEach-Object { Write-Host "    $_" }
                }
            } catch {
                Write-Host "  Data read error: $_" -ForegroundColor Red
            }

            $dataTcp.Close()

            # Read transfer complete
            try {
                $transferResp = $reader.ReadLine()
                Write-Host "  Transfer: $transferResp"
            } catch {}
        }

        # Now try to RETR config.txt
        Write-Host ""
        Write-Host "  --- Reading config.txt ---" -ForegroundColor Yellow
        $writer.WriteLine("PASV")
        $pasvResp = $reader.ReadLine()
        if ($pasvResp -match "\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)") {
            $dataPort = [int]$Matches[5] * 256 + [int]$Matches[6]
            $dataIP = "$($Matches[1]).$($Matches[2]).$($Matches[3]).$($Matches[4])"
            
            $dataTcp = New-Object System.Net.Sockets.TcpClient
            $dataTcp.Connect($dataIP, $dataPort)
            $dataStream = $dataTcp.GetStream()
            $dataStream.ReadTimeout = 5000

            $writer.WriteLine("RETR config.txt")
            $retrResp = $reader.ReadLine()
            Write-Host "  RETR: $retrResp"

            if ($retrResp -match "^150") {
                Start-Sleep -Milliseconds 1000
                $dataBuf = New-Object byte[] 8192
                $n = $dataStream.Read($dataBuf, 0, 8192)
                $content = [Text.Encoding]::ASCII.GetString($dataBuf, 0, $n)
                Write-Host $content
                try { $reader.ReadLine() | Out-Null } catch {}
            }
            $dataTcp.Close()
        }

        $writer.WriteLine("QUIT")
    }
    $tcp.Close()
} catch {
    Write-Host "  ERROR: $_" -ForegroundColor Red
}

# --- 4. GDB RSP Test ---
Write-Section "GDB RSP (31337)"
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.Connect($VitaIP, 31337)
    $s = $tcp.GetStream()
    $s.ReadTimeout = 10000
    $s.WriteTimeout = 3000

    Write-Host "  Connected to GDB port"

    # Send simplest possible GDB packet: $?#3f (halt reason query)
    $packet = [Text.Encoding]::ASCII.GetBytes('$?#3f')
    $s.Write($packet, 0, $packet.Length)
    $s.Flush()
    Write-Host "  Sent: `$?#3f"

    # Wait for response
    $buf = New-Object byte[] 4096
    try {
        $n = $s.Read($buf, 0, 4096)
        if ($n -gt 0) {
            $resp = [Text.Encoding]::ASCII.GetString($buf, 0, $n)
            Write-Host "  Response ($n bytes): $resp" -ForegroundColor Green
        } else {
            Write-Host "  Connection closed by remote (0 bytes)" -ForegroundColor Red
        }
    } catch [System.IO.IOException] {
        Write-Host "  Read timeout (10s) - no response from kvdb" -ForegroundColor Red
    }
    $tcp.Close()
} catch {
    Write-Host "  ERROR: $_" -ForegroundColor Red
}

Write-Section "Done"
