param(
    [switch]$SkipPages  # skip full-page PNG renders (faster, smaller)
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out  = Join-Path $root 'out\text'
$img  = Join-Path $root 'out\figures'
$pag  = Join-Path $root 'out\pages'

foreach ($tool in 'pdftotext.exe', 'pdfimages.exe', 'pdftoppm.exe') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found in PATH. Install Poppler and add its bin folder to PATH."
    }
}

New-Item -ItemType Directory -Force -Path $out, $img, $pag | Out-Null

$pdfs = @(Get-ChildItem -LiteralPath $root -File -Filter '*.pdf')
if ($pdfs.Count -eq 0) {
    Write-Host 'No PDF files found in this folder.'
    exit 1
}

# Output names are ASCII-only (diacritics stripped): poppler fails on non-ASCII output paths on Windows.
function Get-SafeName([string]$name) {
    $decomposed = $name.Normalize([Text.NormalizationForm]::FormD)
    $ascii = -join ($decomposed.ToCharArray() | Where-Object {
        [Globalization.CharUnicodeInfo]::GetUnicodeCategory($_) -ne 'NonSpacingMark'
    })
    return ($ascii -replace '[^A-Za-z0-9]+', '_').Trim('_')
}

foreach ($f in $pdfs) {
    $name = Get-SafeName $f.BaseName
    Write-Host ('Converting: ' + $f.Name + '  ->  ' + $name)

    # Plain text in reading order (no -layout, which mixes up two-column text)
    $txtPath = Join-Path $out ($name + '.txt')
    & pdftotext.exe -enc UTF-8 $f.FullName $txtPath
    if ($LASTEXITCODE -ne 0) { throw ('pdftotext failed on ' + $f.Name) }

    # Embedded figure files, named by page
    $imgDir = Join-Path $img $name
    New-Item -ItemType Directory -Force -Path $imgDir | Out-Null
    & pdfimages.exe -png -p $f.FullName (Join-Path $imgDir 'img')
    if ($LASTEXITCODE -ne 0) { throw ('pdfimages failed on ' + $f.Name) }

    # Full-page renders: needed to read formulas and tables that plain text garbles
    if (-not $SkipPages) {
        $pageDir = Join-Path $pag $name
        New-Item -ItemType Directory -Force -Path $pageDir | Out-Null
        & pdftoppm.exe -png -r 120 $f.FullName (Join-Path $pageDir 'page')
        if ($LASTEXITCODE -ne 0) { throw ('pdftoppm failed on ' + $f.Name) }
    }
}

Write-Host ('Done. Text in ' + $out + ', figures in ' + $img + $(if (-not $SkipPages) { ', pages in ' + $pag } else { '' }))