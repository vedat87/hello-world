param(
    [string]$SourceRoot = ".",
    [string]$File = "",
    [switch]$Restore
)

$ErrorActionPreference = "Stop"

function Read-SourceFile([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $enc = New-Object System.Text.UTF8Encoding($true)
        $text = $enc.GetString($bytes, 3, $bytes.Length - 3)
        return @{ Text = $text; Encoding = $enc; Bom = [byte[]](0xEF,0xBB,0xBF) }
    }

    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $enc = [System.Text.Encoding]::Unicode
        $text = $enc.GetString($bytes, 2, $bytes.Length - 2)
        return @{ Text = $text; Encoding = $enc; Bom = [byte[]](0xFF,0xFE) }
    }

    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        $enc = [System.Text.Encoding]::BigEndianUnicode
        $text = $enc.GetString($bytes, 2, $bytes.Length - 2)
        return @{ Text = $text; Encoding = $enc; Bom = [byte[]](0xFE,0xFF) }
    }

    $enc = [System.Text.Encoding]::Default
    $text = $enc.GetString($bytes)
    return @{ Text = $text; Encoding = $enc; Bom = [byte[]]@() }
}

function Write-SourceFile([string]$Path, [string]$Text, $Encoding, [byte[]]$Bom) {
    $body = $Encoding.GetBytes($Text)
    if ($Bom.Length -gt 0) {
        $out = New-Object byte[] ($Bom.Length + $body.Length)
        [Array]::Copy($Bom, 0, $out, 0, $Bom.Length)
        [Array]::Copy($body, 0, $out, $Bom.Length, $body.Length)
        [System.IO.File]::WriteAllBytes($Path, $out)
    }
    else {
        [System.IO.File]::WriteAllBytes($Path, $body)
    }
}

function Find-WSClient([string]$Root) {
    $all = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "WSclient.cpp" -ErrorAction Stop)
    $matched = @()

    foreach ($f in $all) {
        try {
            $raw = Read-SourceFile $f.FullName
            if ($raw.Text.Contains("MASTER_SKILL_ADD_GREATER_LIFE_MASTERED") -and
                $raw.Text.Contains("EFFECT_GREATER_LIFE_ENHANCED") -and
                $raw.Text.Contains("EFFECT_GREATER_LIFE_MASTERED")) {
                $matched += $f
            }
        }
        catch { }
    }

    if ($matched.Count -eq 1) {
        return $matched[0].FullName
    }

    if ($matched.Count -eq 0) {
        throw "Uygun WSclient.cpp bulunamadi. -File ile dosya yolunu acikca verin."
    }

    Write-Host "Birden fazla uygun WSclient.cpp bulundu:" -ForegroundColor Yellow
    $matched | ForEach-Object { Write-Host ("  " + $_.FullName) }
    throw "-File parametresi ile derlenen Main'e ait WSclient.cpp dosyasini secin."
}

if ([string]::IsNullOrWhiteSpace($File)) {
    $File = Find-WSClient (Resolve-Path -LiteralPath $SourceRoot).Path
}
else {
    $File = (Resolve-Path -LiteralPath $File).Path
}

$backup = $File + ".inner135136_v2.bak"

if ($Restore) {
    if (-not (Test-Path -LiteralPath $backup)) {
        throw "Yedek bulunamadi: $backup"
    }
    Copy-Item -LiteralPath $backup -Destination $File -Force
    Write-Host "RESTORED: $File" -ForegroundColor Green
    exit 0
}

$src = Read-SourceFile $File
$text = $src.Text
$nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

if ($text.Contains("INNER_135_136_NOBITMAPLIGHT_V2")) {
    Write-Host "Patch zaten uygulanmis: $File" -ForegroundColor Yellow
    exit 0
}

# -----------------------------------------------------------------------------
# PATCH 1: InsertBuffPhysicalEffect
# Original custom Main groups 135/136 with eBuff_HpRecovery and recreates
# BITMAP_LIGHT. Keep normal Greater Life visual on eBuff_HpRecovery, but make
# master 135/136 logical-only for this diagnostic/fix build.
# -----------------------------------------------------------------------------
$rx1 = New-Object System.Text.RegularExpressions.Regex(
    '(?ms)(?<ind>^[ \t]*)case\s+eBuff_HpRecovery\s*:\s*\r?\n' +
    '[ \t]*case\s+EFFECT_GREATER_LIFE_ENHANCED\s*:\s*\r?\n' +
    '[ \t]*case\s+EFFECT_GREATER_LIFE_MASTERED\s*:\s*\r?\n' +
    '[ \t]*\{\s*\r?\n' +
    '[ \t]*DeleteEffect\s*\(\s*BITMAP_LIGHT\s*,\s*o\s*,\s*1\s*\)\s*;\s*\r?\n' +
    '[ \t]*CreateEffect\s*\(\s*BITMAP_LIGHT\s*,\s*o->Position\s*,\s*o->Angle\s*,\s*o->Light\s*,\s*1\s*,\s*o\s*\)\s*;\s*\r?\n' +
    '[ \t]*\}\s*\r?\n[ \t]*break\s*;'
)

$m1 = $rx1.Matches($text)
if ($m1.Count -ne 1) {
    throw "PATCH 1 guvenlik kontrolu basarisiz. Beklenen grup sayisi=1, bulunan=$($m1.Count). Dosya degistirilmedi."
}

$ind1 = $m1[0].Groups['ind'].Value
$rep1 = @(
    $ind1 + 'case eBuff_HpRecovery:',
    $ind1 + '{',
    $ind1 + "`tDeleteEffect(BITMAP_LIGHT, o, 1);",
    $ind1 + "`tCreateEffect(BITMAP_LIGHT, o->Position, o->Angle, o->Light, 1, o);",
    $ind1 + '}',
    $ind1 + 'break;',
    '',
    $ind1 + 'case EFFECT_GREATER_LIFE_ENHANCED:',
    $ind1 + 'case EFFECT_GREATER_LIFE_MASTERED:',
    $ind1 + '{',
    $ind1 + "`t// INNER_135_136_NOBITMAPLIGHT_V2",
    $ind1 + "`t// Keep logical/master HP buff; do not create a second BITMAP_LIGHT.",
    $ind1 + '}',
    $ind1 + 'break;'
) -join $nl
$text = $rx1.Replace($text, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $rep1 }, 1)

# -----------------------------------------------------------------------------
# PATCH 2: ClearBuffPhysicalEffect
# Do not delete a physical light for master 135/136 because this patch no longer
# creates one there. Normal eBuff_HpRecovery remains unchanged.
# -----------------------------------------------------------------------------
$rx2 = New-Object System.Text.RegularExpressions.Regex(
    '(?ms)(?<ind>^[ \t]*)case\s+eBuff_HpRecovery\s*:\s*\r?\n' +
    '[ \t]*case\s+EFFECT_GREATER_LIFE_ENHANCED\s*:\s*\r?\n' +
    '[ \t]*case\s+EFFECT_GREATER_LIFE_MASTERED\s*:\s*\r?\n' +
    '[ \t]*\{\s*\r?\n' +
    '[ \t]*DeleteEffect\s*\(\s*BITMAP_LIGHT\s*,\s*o\s*,\s*1\s*\)\s*;\s*\r?\n' +
    '[ \t]*\}\s*\r?\n[ \t]*break\s*;'
)

$m2 = $rx2.Matches($text)
if ($m2.Count -ne 1) {
    throw "PATCH 2 guvenlik kontrolu basarisiz. Beklenen grup sayisi=1, bulunan=$($m2.Count). Dosya degistirilmedi."
}

$ind2 = $m2[0].Groups['ind'].Value
$rep2 = @(
    $ind2 + 'case eBuff_HpRecovery:',
    $ind2 + '{',
    $ind2 + "`tDeleteEffect(BITMAP_LIGHT, o, 1);",
    $ind2 + '}',
    $ind2 + 'break;',
    '',
    $ind2 + 'case EFFECT_GREATER_LIFE_ENHANCED:',
    $ind2 + 'case EFFECT_GREATER_LIFE_MASTERED:',
    $ind2 + '{',
    $ind2 + "`t// INNER_135_136_NOBITMAPLIGHT_V2: nothing to clear here.",
    $ind2 + '}',
    $ind2 + 'break;'
) -join $nl
$text = $rx2.Replace($text, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $rep2 }, 1)

# -----------------------------------------------------------------------------
# PATCH 3: received Inner animation path
# Original uses OR, therefore the visual block is almost always entered. For
# master Enhanced/Mastered (360/363), skip this BITMAP_LIGHT recreation entirely.
# For normal/older Greater Life paths, require that none of the three buffs is
# already active before creating the light.
# -----------------------------------------------------------------------------
$rx3 = New-Object System.Text.RegularExpressions.Regex(
    '(?ms)if\s*\(\s*' +
    '!g_isCharacterBuff\s*\(\s*to\s*,\s*eBuff_HpRecovery\s*\)\s*\|\|\s*' +
    '!g_isCharacterBuff\s*\(\s*to\s*,\s*EFFECT_GREATER_LIFE_ENHANCED\s*\)\s*\|\|\s*' +
    '!g_isCharacterBuff\s*\(\s*to\s*,\s*EFFECT_GREATER_LIFE_MASTERED\s*\)\s*\)'
)

$m3 = $rx3.Matches($text)
if ($m3.Count -ne 1) {
    throw "PATCH 3 guvenlik kontrolu basarisiz. Beklenen Inner if sayisi=1, bulunan=$($m3.Count). Dosya degistirilmedi."
}

$rep3 = @(
    'if (MagicNumber != MASTER_SKILL_ADD_GREATER_LIFE_ENHANCED &&',
    "`tMagicNumber != MASTER_SKILL_ADD_GREATER_LIFE_MASTERED &&",
    "`t!g_isCharacterBuff(to, eBuff_HpRecovery) &&",
    "`t!g_isCharacterBuff(to, EFFECT_GREATER_LIFE_ENHANCED) &&",
    "`t!g_isCharacterBuff(to, EFFECT_GREATER_LIFE_MASTERED))"
) -join $nl
$text = $rx3.Replace($text, $rep3, 1)

# Final safety checks BEFORE touching the source file.
if (-not $text.Contains("INNER_135_136_NOBITMAPLIGHT_V2")) {
    throw "Final marker missing. Dosya degistirilmedi."
}
if (-not $text.Contains("MASTER_SKILL_ADD_GREATER_LIFE_ENHANCED") -or
    -not $text.Contains("MASTER_SKILL_ADD_GREATER_LIFE_MASTERED")) {
    throw "Master skill markers unexpectedly missing. Dosya degistirilmedi."
}

if (-not (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $File -Destination $backup -Force
}

Write-SourceFile $File $text $src.Encoding $src.Bom

Write-Host "" 
Write-Host "PATCH APPLIED" -ForegroundColor Green
Write-Host "File  : $File"
Write-Host "Backup: $backup"
Write-Host "" 
Write-Host "Degisiklikler:" -ForegroundColor Cyan
Write-Host "  1) InsertBuffPhysicalEffect: Effect 135/136 BITMAP_LIGHT kapali"
Write-Host "  2) ClearBuffPhysicalEffect : Effect 135/136 BITMAP_LIGHT delete kapali"
Write-Host "  3) Inner receive path       : 360/363 icin ikinci BITMAP_LIGHT recreate kapali"
Write-Host "" 
Write-Host "Skill 356/360/363, Effect 135/136 ve logical HP buff KAPATILMADI." -ForegroundColor Green
Write-Host "VS2026 -> Release | Win32/x86 derleyip yeni Main.exe ile test edin." -ForegroundColor Yellow
Write-Host "Geri almak icin: .\APPLY_INNER_135_136_NOBITMAPLIGHT_V2.ps1 -File `"$File`" -Restore"
