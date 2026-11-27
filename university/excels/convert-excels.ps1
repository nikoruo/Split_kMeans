param(
    [switch]$SkipCharts  # skip chart PNG export (metadata is still written)
)

# Converts every workbook in this folder with Excel itself (read-only, macros disabled):
#   out\csv\<book>\NN_<sheet>.csv           cell values, CSV row/column = Excel row/column (row 1 = 1, column 1 = A)
#   out\csv\<book>\NN_<sheet>.formulas.txt  formulas; filled-down blocks collapsed to one line
#   out\charts\<book>\*.png                 chart images
#   out\index\<book>.txt                    sheets, pivot table definitions, chart types, axes and series ranges

$ErrorActionPreference = 'Stop'
$root   = $PSScriptRoot
$outCsv = Join-Path $root 'out\csv'
$outImg = Join-Path $root 'out\charts'
$outIdx = Join-Path $root 'out\index'

# Excel creates ~$name.xlsx lock files while a workbook is open; skip them.
$books = @(Get-ChildItem -LiteralPath $root -File | Where-Object {
    ($_.Extension -in '.xlsx', '.xlsm', '.xls') -and -not $_.Name.StartsWith('~$')
})
if ($books.Count -eq 0) {
    Write-Host 'No Excel files found in this folder.'
    exit 1
}
New-Item -ItemType Directory -Force -Path $outCsv, $outImg, $outIdx | Out-Null

$inv  = [Globalization.CultureInfo]::InvariantCulture
$utf8 = New-Object System.Text.UTF8Encoding($false)

$cellErrors = @{
    -2146826281 = '#DIV/0!'; -2146826246 = '#N/A'; -2146826259 = '#NAME?'; -2146826288 = '#NULL!'
    -2146826252 = '#NUM!'; -2146826265 = '#REF!'; -2146826273 = '#VALUE!'
}
$pivotFunctions = @{
    -4157 = 'Sum'; -4112 = 'Count'; -4106 = 'Average'; -4136 = 'Max'; -4139 = 'Min'; -4149 = 'Product'
    -4113 = 'CountNums'; -4155 = 'StDev'; -4156 = 'StDevP'; -4164 = 'Var'; -4165 = 'VarP'
}
$chartTypes = @{
    -4169 = 'XY scatter'; 74 = 'XY scatter, lines'; 75 = 'XY scatter, lines no markers'
    72 = 'XY scatter, smooth'; 73 = 'XY scatter, smooth no markers'; 4 = 'Line'; 65 = 'Line, markers'
    51 = 'Clustered column'; 52 = 'Stacked column'; 57 = 'Clustered bar'; 58 = 'Stacked bar'
    1 = 'Area'; 5 = 'Pie'; 118 = 'Histogram'; 121 = 'Box and whisker'
}

# ASCII-only names, consistent with the articles script.
function Get-SafeName([string]$name) {
    $decomposed = $name.Normalize([Text.NormalizationForm]::FormD)
    $ascii = -join ($decomposed.ToCharArray() | Where-Object {
        [Globalization.CharUnicodeInfo]::GetUnicodeCategory($_) -ne 'NonSpacingMark'
    })
    $safe = ($ascii -replace '[^A-Za-z0-9]+', '_').Trim('_')
    if ($safe) { return $safe } else { return 'sheet' }
}

function Get-ColName([int]$c) {
    $s = ''
    while ($c -gt 0) {
        $m = ($c - 1) % 26
        $s = [string][char](65 + $m) + $s
        $c = ($c - $m - 1) / 26
    }
    return $s
}

function Add-Line([string]$s) { [void]$script:idx.AppendLine($s) }

# Excel returns a scalar instead of a 1-based 2D array when the range is a single cell.
function ConvertTo-Grid($v) {
    if ($v -is [Array]) { return ,$v }
    $g = [Array]::CreateInstance([object], [int[]](1, 1), [int[]](1, 1))
    $g.SetValue($v, 1, 1)
    return ,$g
}

function Format-Cell($v) {
    if ($v -is [bool]) { if ($v) { return 'TRUE' } else { return 'FALSE' } }
    if ($v -is [int]) {
        if ($cellErrors.ContainsKey($v)) { return $cellErrors[$v] }
        return $v.ToString($inv)
    }
    # Line breaks become a literal \n so CSV line N stays Excel row N.
    $s = ([string]$v) -replace "`r`n|`r|`n", '\n'
    if ($s.IndexOfAny([char[]]',"') -ge 0) { return '"' + $s.Replace('"', '""') + '"' }
    return $s
}

function Export-Values($vals, [int]$nr, [int]$nc, [string]$path) {
    $sb = New-Object System.Text.StringBuilder
    for ($r = 1; $r -le $nr; $r++) {
        for ($c = 1; $c -le $nc; $c++) {
            if ($c -gt 1) { [void]$sb.Append(',') }
            $v = $vals.GetValue($r, $c)
            if ($null -eq $v) { continue }
            if ($v -is [double]) {
                $s = $v.ToString('R', $inv)
                # 'R' occasionally fails to round-trip on .NET Framework (Windows PowerShell 5.1).
                if ([double]::Parse($s, $inv) -ne $v) { $s = $v.ToString('G17', $inv) }
                [void]$sb.Append($s)
            }
            else { [void]$sb.Append((Format-Cell $v)) }
        }
        [void]$sb.AppendLine()
    }
    [IO.File]::WriteAllText($path, $sb.ToString(), $utf8)
}

# Returns the number of formula blocks written (0 = no file).
function Export-Formulas($fA1, $fR1C1, [int]$nr, [int]$nc, [string]$path) {
    $sb = New-Object System.Text.StringBuilder
    $blocks = 0
    for ($c = 1; $c -le $nc; $c++) {
        $col = Get-ColName $c
        $r = 1
        while ($r -le $nr) {
            $f = [string]$fR1C1.GetValue($r, $c)
            if (-not $f.StartsWith('=')) { $r++; continue }
            $end = $r
            while ($end -lt $nr -and [string]$fR1C1.GetValue($end + 1, $c) -eq $f) { $end++ }
            $addr = $col + $r
            if ($end -gt $r) { $addr = $addr + ':' + $col + $end }
            [void]$sb.AppendLine($addr + '  ' + [string]$fA1.GetValue($r, $c))
            $blocks++
            $r = $end + 1
        }
    }
    if ($blocks -gt 0) { [IO.File]::WriteAllText($path, $sb.ToString(), $utf8) }
    return $blocks
}

# COM collections are read by index: foreach over them is unreliable in PowerShell 7.
function Get-Items($coll) {
    $list = @()
    for ($i = 1; $i -le $coll.Count; $i++) { $list += $coll.Item($i) }
    return ,$list
}

# RowFields etc. are parameterized properties and need Invoke(). Excel may throw instead of
# returning an empty collection when a pivot has no fields of a kind.
function Get-PivotFields($pt, [string]$kind) {
    try { return ,(Get-Items $pt.$kind.Invoke()) } catch { return ,@() }
}

function Get-Names($fields) {
    if ($fields.Count -eq 0) { return '(none)' }
    return (@($fields | ForEach-Object { [string]$_.Name }) -join ', ')
}

function Add-PivotInfo($pt) {
    Add-Line ('  Pivot table ''' + $pt.Name + ''' at ' + $pt.TableRange2.Address($false, $false))
    try {
        $src = [string]$excel.ConvertFormula('=' + $pt.SourceData, -4150, 1, 1)
        Add-Line ('    Source: ' + ($src.TrimStart('=') -replace '\[[^\]]*\]', ''))
    } catch {
        Add-Line '    Source: (not a worksheet range, e.g. data model or external)'
    }
    $rows  = Get-PivotFields $pt 'RowFields'
    $cols  = Get-PivotFields $pt 'ColumnFields'
    $pages = Get-PivotFields $pt 'PageFields'
    Add-Line ('    Rows: ' + (Get-Names $rows))
    Add-Line ('    Columns: ' + (Get-Names $cols))

    $filters = @()
    foreach ($pf in $pages) {
        $page = try { [string]$pf.CurrentPage.Name } catch { '?' }
        $filters += ($pf.Name + ' = ' + $page)
    }
    if ($filters.Count -eq 0) { $filters = @('(none)') }
    Add-Line ('    Filters: ' + ($filters -join '; '))

    try {
        foreach ($df in (Get-PivotFields $pt 'DataFields')) {
            $fn = $pivotFunctions[[int]$df.Function]
            if (-not $fn) { $fn = 'function ' + $df.Function }
            $line = '    Value: ''' + $df.Name + ''' = ' + $fn + '(' + $df.SourceName + ')'
            if ($df.Calculation -ne -4143) { $line += ', shown as calculation ' + $df.Calculation }
            Add-Line $line
        }
    } catch { Add-Line ('    Values: could not read (' + $_.Exception.Message + ')') }

    # Hidden items and label/value filters change what the pivot shows compared with the raw data.
    foreach ($fld in ($rows + $cols + $pages)) {
        try {
            $hidden = @(Get-Items $fld.HiddenItems.Invoke() | ForEach-Object { [string]$_.Name })
            if ($hidden.Count -gt 0) {
                $shown = ($hidden | Select-Object -First 30) -join ', '
                if ($hidden.Count -gt 30) { $shown += (', ... (' + $hidden.Count + ' in total)') }
                Add-Line ('    Hidden items in ''' + $fld.Name + ''': ' + $shown)
            }
        } catch { }
        try {
            if ($fld.PivotFilters.Count -gt 0) {
                Add-Line ('    ''' + $fld.Name + ''' has ' + $fld.PivotFilters.Count + ' label/value filter(s)')
            }
        } catch { }
    }
}

function Add-ChartInfo($chart, [string]$label, [string]$pngPath) {
    if (-not $SkipCharts) {
        # Excel cannot save to paths over 218 characters, so export to a short temp path first.
        $tmp = Join-Path $env:TEMP ('xlchart_' + [guid]::NewGuid().ToString('N') + '.png')
        [void]$chart.Export($tmp, 'PNG')
        Move-Item -LiteralPath $tmp -Destination $pngPath -Force
        $label += ' -> ' + $pngPath.Substring($script:outRoot.Length + 1)
    }
    Add-Line ('  ' + $label)

    $type = $chartTypes[[int]$chart.ChartType]
    if (-not $type) { $type = 'chart type ' + $chart.ChartType }
    $title = '(none)'
    try { if ($chart.HasTitle) { $title = '''' + $chart.ChartTitle.Text + '''' } } catch { }
    Add-Line ('    Type: ' + $type + '; title: ' + $title)
    try {
        $pl = $chart.PivotLayout
        if ($null -ne $pl) { Add-Line ('    Pivot chart of ''' + $pl.PivotTable.Name + '''') }
    } catch { }

    foreach ($t in 1, 2) {
        try { $ax = $chart.Axes($t) } catch { continue }
        $parts = @()
        try { if ($ax.HasTitle) { $parts += ('title ''' + $ax.AxisTitle.Text + '''') } } catch { }
        try { if ($ax.ScaleType -eq -4133) { $parts += 'log scale' } } catch { }
        # Category axes of line and column charts return null here, so compare with $false explicitly.
        try {
            if ($ax.MinimumScaleIsAuto -eq $false) { $parts += ('min ' + ([double]$ax.MinimumScale).ToString('R', $inv)) }
            if ($ax.MaximumScaleIsAuto -eq $false) { $parts += ('max ' + ([double]$ax.MaximumScale).ToString('R', $inv)) }
        } catch { }
        if ($parts.Count -eq 0) { $parts = @('no title') }
        $axName = if ($t -eq 1) { 'Category (X) axis' } else { 'Value (Y) axis' }
        Add-Line ('    ' + $axName + ': ' + ($parts -join ', '))
    }

    try { $series = Get-Items $chart.SeriesCollection() } catch { $series = @() }
    foreach ($s in $series) {
        $name = try { [string]$s.Name } catch { '?' }
        $ref = try { [string]$s.Formula } catch { $null }
        if (-not $ref) {
            try { $ref = '(no range) ' + @($s.Values).Count + ' values' } catch { $ref = '(no range)' }
        }
        Add-Line ('    Series ''' + $name + ''': ' + $ref)
    }
}

$script:outRoot = Join-Path $root 'out'
$excel = New-Object -ComObject Excel.Application
try {
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false
    $excel.EnableEvents = $false
    $excel.AutomationSecurity = 3  # msoAutomationSecurityForceDisable: never run macros

    foreach ($f in $books) {
        $book = Get-SafeName $f.BaseName
        Write-Host ('Converting: ' + $f.Name + '  ->  ' + $book)
        $csvDir = Join-Path $outCsv $book
        $imgDir = Join-Path $outImg $book
        # Start clean so renamed or deleted sheets leave no stale files.
        foreach ($d in $csvDir, $imgDir) {
            if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
            New-Item -ItemType Directory -Force -Path $d | Out-Null
        }

        $script:idx = New-Object System.Text.StringBuilder
        Add-Line ('Workbook: ' + $f.Name)
        Add-Line 'CSV: line N = Excel row N, field 1 = column A. Numbers are raw values (full precision, dot decimal),'
        Add-Line 'so percentages appear as fractions and dates as Excel serial numbers. Pivot tables are exported as last saved.'

        # UpdateLinks = 0, ReadOnly = true
        $wb = $excel.Workbooks.Open($f.FullName, 0, $true)
        try {
            foreach ($ws in (Get-Items $wb.Worksheets)) {
                $prefix = ([int]$ws.Index).ToString('00') + '_' + (Get-SafeName $ws.Name)
                $vis = switch ([int]$ws.Visible) { -1 { 'visible' } 0 { 'hidden' } default { 'very hidden' } }
                Write-Host ('  Sheet: ' + $ws.Name)

                $ur = $ws.UsedRange
                $nr = $ur.Row + $ur.Rows.Count - 1
                $nc = $ur.Column + $ur.Columns.Count - 1
                $rng = $ws.Range($ws.Cells.Item(1, 1), $ws.Cells.Item($nr, $nc))

                Add-Line ''
                Add-Line ('== Sheet ' + $ws.Index + ' ''' + $ws.Name + ''' (' + $vis + '), A1:' + (Get-ColName $nc) + $nr +
                          ' (' + $nr + ' rows x ' + $nc + ' columns) -> csv\' + $book + '\' + $prefix + '.csv')

                Export-Values (ConvertTo-Grid $rng.Value2) $nr $nc (Join-Path $csvDir ($prefix + '.csv'))

                # Formula2 keeps dynamic-array formulas as written; older Excel only has Formula.
                $fA1 = $rng.Formula2
                if ($null -eq $fA1) { $fA1 = $rng.Formula }
                $fR1C1 = $rng.Formula2R1C1
                if ($null -eq $fR1C1) { $fR1C1 = $rng.FormulaR1C1 }
                $blocks = Export-Formulas (ConvertTo-Grid $fA1) (ConvertTo-Grid $fR1C1) $nr $nc (Join-Path $csvDir ($prefix + '.formulas.txt'))
                if ($blocks -gt 0) {
                    Add-Line ('  Formulas: ' + $blocks + ' block(s) -> csv\' + $book + '\' + $prefix + '.formulas.txt')
                }

                foreach ($pt in (Get-Items $ws.PivotTables())) {
                    try { Add-PivotInfo $pt } catch { Add-Line ('  Pivot table: could not read (' + $_.Exception.Message + ')') }
                }

                $i = 0
                foreach ($co in (Get-Items $ws.ChartObjects())) {
                    $i++
                    $png = Join-Path $imgDir ($prefix + '_chart' + $i.ToString('00') + '.png')
                    $label = 'Chart ' + $i + ' ''' + $co.Name + ''' at ' + $co.TopLeftCell.Address($false, $false)
                    try { Add-ChartInfo $co.Chart $label $png } catch { Add-Line ('  ' + $label + ': could not export (' + $_.Exception.Message + ')') }
                }
            }

            foreach ($cs in (Get-Items $wb.Charts)) {
                $prefix = ([int]$cs.Index).ToString('00') + '_' + (Get-SafeName $cs.Name)
                Write-Host ('  Chart sheet: ' + $cs.Name)
                Add-Line ''
                Add-Line ('== Chart sheet ' + $cs.Index + ' ''' + $cs.Name + '''')
                $png = Join-Path $imgDir ($prefix + '.png')
                try { Add-ChartInfo $cs 'Chart' $png } catch { Add-Line ('  Chart: could not export (' + $_.Exception.Message + ')') }
            }
        } finally {
            $wb.Close($false)
        }
        [IO.File]::WriteAllText((Join-Path $outIdx ($book + '.txt')), $idx.ToString(), $utf8)
    }
} finally {
    $excel.Quit()
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}

Write-Host ('Done. Output in ' + $script:outRoot)
