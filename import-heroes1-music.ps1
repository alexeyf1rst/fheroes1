param(
    [Parameter(Mandatory = $true)]
    [string]$SoundDirectory,
    [string]$MusicDirectory = (Join-Path $PSScriptRoot 'music')
)

$ErrorActionPreference = 'Stop'
$tracks = @(Get-ChildItem -LiteralPath $SoundDirectory -File | Where-Object {
    $_.Name -match '^Heroes\d{2}\.(62s|82m|ogg)$'
} | Sort-Object @{ Expression = { $_.Extension -ne '.ogg' } }, Name)
if ($tracks.Count -eq 0) {
    throw "No original Heroes I music tracks were found in $SoundDirectory"
}

New-Item -ItemType Directory -Path $MusicDirectory -Force | Out-Null
$imported = @{}
foreach ($track in $tracks) {
    if ($imported.ContainsKey($track.BaseName)) {
        continue
    }
    $imported[$track.BaseName] = $true
    $extension = if ($track.Extension -eq '.ogg') { '.ogg' } else { '.wav' }
    $destination = Join-Path $MusicDirectory ($track.BaseName + $extension)
    if (Test-Path -LiteralPath $destination) {
        continue
    }
    if ($extension -eq '.ogg') {
        Copy-Item -LiteralPath $track.FullName -Destination $destination
        continue
    }

    $stereo = $track.Extension -eq '.62s'
    $channels = if ($stereo) { 2 } else { 1 }
    $bitsPerSample = if ($stereo) { 16 } else { 8 }
    $blockSize = [int]($channels * $bitsPerSample / 8)
    $samples = [IO.File]::ReadAllBytes($track.FullName)
    $dataSize = $samples.Length - ($samples.Length % $blockSize)
    if ($dataSize -eq 0) {
        continue
    }
    if (!$stereo) {
        # Heroes I mono PCM is signed; the WAV container expects unsigned bytes.
        for ($i = 0; $i -lt $dataSize; ++$i) {
            $samples[$i] = $samples[$i] -bxor 128
        }
    }

    $temporary = $destination + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    $padding = $dataSize % 2
    try {
        $writer = [IO.BinaryWriter]::new([IO.File]::Create($temporary))
        try {
            $writer.Write([uint32]0x46464952)
            $writer.Write([uint32](36 + $dataSize + $padding))
            $writer.Write([uint32]0x45564157)
            $writer.Write([uint32]0x20746D66)
            $writer.Write([uint32]16)
            $writer.Write([uint16]1)
            $writer.Write([uint16]$channels)
            $writer.Write([uint32]22050)
            $writer.Write([uint32](22050 * $blockSize))
            $writer.Write([uint16]$blockSize)
            $writer.Write([uint16]$bitsPerSample)
            $writer.Write([uint32]0x61746164)
            $writer.Write([uint32]$dataSize)
            $writer.Write($samples, 0, $dataSize)
            if ($padding) {
                $writer.Write([byte]0)
            }
        }
        finally {
            $writer.Dispose()
        }
        Move-Item -LiteralPath $temporary -Destination $destination
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary
        }
    }
}
