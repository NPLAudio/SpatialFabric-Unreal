# Optional Windows/PowerPoint rendering. Opens only the generated deck read-only,
# closes only that presentation, and never quits an existing user's PowerPoint session.
param([string]$PreviewDirectory = (Join-Path $env:TEMP ("SpatialFabricSlides-" + [guid]::NewGuid())))
$ErrorActionPreference = 'Stop'
$PreviewDirectory = [IO.Path]::GetFullPath($PreviewDirectory)
$root = Split-Path $PSScriptRoot -Parent
$deck = Join-Path $root 'Presentations/SpatialFabric_From_Cue_to_Space.pptx'
$pdf = Join-Path $root 'Presentations/SpatialFabric_From_Cue_to_Space.pdf'
if (!(Test-Path $deck)) { throw 'Build the PowerPoint first.' }
$hadApp = $false
try { $app = [Runtime.InteropServices.Marshal]::GetActiveObject('PowerPoint.Application'); $hadApp = $true }
catch { $app = New-Object -ComObject PowerPoint.Application }
$presentation = $null
try {
    $presentation = $app.Presentations.Open($deck, -1, 0, 0)
    $presentation.Export($PreviewDirectory, 'PNG', 1600, 900)
    $presentation.SaveAs($pdf, 32)
    Write-Output ("Rendered " + $presentation.Slides.Count + " slides; PDF: " + $pdf)
    Write-Output ("Preview frames: " + $PreviewDirectory)
}
finally {
    if ($presentation) { $presentation.Close() }
    if (!$hadApp -and $app.Presentations.Count -eq 0) { $app.Quit() }
}
