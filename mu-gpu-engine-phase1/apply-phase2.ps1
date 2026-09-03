param([string]$Path = 'out/GpuSkinningLegacy.cpp')

$c = Get-Content $Path -Raw

if ($c -notmatch 'gLastPaletteSerial') {
    $c = $c.Replace('    std::string gError;', "    std::string gError;`r`n    std::uint32_t gLastPaletteSerial = 0;")
}

$pattern = '(?s)        // Upload one compact 3x4 affine palette\. 200 bones = 9\.6 KB max\..*?        glUseProgram_\(gProgram\);'
$replacement = @'
        // Phase2 hot path: one bone-palette upload per BMD transform serial.
        // All meshes of the same character/monster share this palette.
        if (gLastPaletteSerial != s.paletteSerial)
        {
            glActiveTexture_(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gBoneTexture);
            const int rows = s.boneCount > 200 ? 200 : s.boneCount;
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 3, rows, GL_RGBA, GL_FLOAT, s.boneRows);
            gLastPaletteSerial = s.paletteSerial;
        }

        glUseProgram_(gProgram);
'@
$new = [regex]::Replace($c, $pattern, $replacement)
if ($new -eq $c) { throw 'Phase2 palette patch did not match GpuSkinningLegacy.cpp' }
$c = $new

# Avoid a synchronous glGetError query for every mesh in the production draw loop.
$c = $c.Replace('        return glGetError() == GL_NO_ERROR;', '        return true;')

if ($c -notmatch 'gLastPaletteSerial = 0;\r?\n        gAvailable = false;') {
    $c = $c.Replace('        gAvailable = false;', "        gLastPaletteSerial = 0;`r`n        gAvailable = false;")
}

Set-Content -Path $Path -Value $c -Encoding ascii
