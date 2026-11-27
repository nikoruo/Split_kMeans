$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$outMd = Join-Path $root 'out\md'
$outImg = Join-Path $root 'out\img'
New-Item -ItemType Directory -Force -Path $outMd, $outImg | Out-Null

$pptFiles = @(Get-ChildItem -Path $root -File | Where-Object { $_.Extension -eq '.ppt' -or $_.Extension -eq '.pptx' })
$pdfFiles = @(Get-ChildItem -Path $root -File -Filter '*.pdf')

# PowerPoint: text, notes and one PNG per slide
if ($pptFiles.Count -gt 0) {
    $ppt = New-Object -ComObject PowerPoint.Application
    foreach ($f in $pptFiles) {
        $name = $f.BaseName
        Write-Host ('PPT: ' + $f.Name)
        $imgDir = Join-Path $outImg $name
        New-Item -ItemType Directory -Force -Path $imgDir | Out-Null

        $pres = $ppt.Presentations.Open($f.FullName, -1, 0, 0)
        $sb = New-Object System.Text.StringBuilder
        [void]$sb.AppendLine('# ' + $name)

        $i = 0
        foreach ($slide in $pres.Slides) {
            $i = $i + 1
            $imgRel = 'slide-' + $i.ToString('000') + '.png'
            $slide.Export((Join-Path $imgDir $imgRel), 'PNG', 1600, 900)

            [void]$sb.AppendLine('')
            [void]$sb.AppendLine('## Slide ' + $i)
            [void]$sb.AppendLine('![Slide ' + $i + '](../img/' + $name + '/' + $imgRel + ')')

            foreach ($shape in $slide.Shapes) {
                if ($shape.HasTextFrame -and $shape.TextFrame.HasText) {
                    [void]$sb.AppendLine($shape.TextFrame.TextRange.Text)
                }
            }
            foreach ($s in $slide.NotesPage.Shapes) {
                if ($s.Type -eq 14 -and $s.PlaceholderFormat.Type -eq 2 -and $s.TextFrame.HasText) {
                    [void]$sb.AppendLine('Notes: ' + $s.TextFrame.TextRange.Text)
                }
            }
        }
        $pres.Close()
        Set-Content -Path (Join-Path $outMd ($name + '.md')) -Value $sb.ToString() -Encoding UTF8
    }
    $ppt.Quit()
}

# Word: text from PDFs
if ($pdfFiles.Count -gt 0) {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    foreach ($f in $pdfFiles) {
        $name = $f.BaseName
        Write-Host ('PDF: ' + $f.Name)
        $doc = $word.Documents.Open($f.FullName, $false, $true)
        # 7 = plain text (wdFormatUnicodeText)
        $doc.SaveAs2((Join-Path $outMd ($name + '.md')), 7)
        $doc.Close($false)
    }
    $word.Quit()
}

Write-Host ('Done. Output in ' + (Join-Path $root 'out'))