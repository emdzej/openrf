# OpenRF @VERSION@ (openrf.wasm) on the bundled gasm-run @GASM_VERSION@: Windows launcher, started by OpenRF.cmd.
#   OpenRF.cmd [options] [CD] [gasm-run options...]          (OpenRF.cmd --help)
# The CD location comes from the argument (then saved), else %APPDATA%\OpenRF\cd-location, else a folder or
# file picker. Test hooks (no dialogs): OPENRF_CD=<folder|image> uses that CD without saving it,
# OPENRF_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfDir = Join-Path $env:APPDATA 'OpenRF'
$LocFile = Join-Path $ConfDir 'cd-location'
$Title = 'Return Fire (gasm)'
$Dry = $env:OPENRF_DRY_RUN -eq '1'
$Change = $false

function Show-Usage {
  @"
OpenRF @VERSION@ on gasm-run @GASM_VERSION@

  OpenRF.cmd [options] [CD] [gasm-run options...]

CD is your Return Fire CD: the disc's drive (e.g. D:\) or a mounted disc image, a folder you copied
the CD to (it has RFIRE.BIN and ART\ART.CAR), or a raw .bin or an .iso image file (for a .bin/.cue
pair, the .bin). It is saved in
  $LocFile
so later runs need no argument. Without one, a dialog asks for it.

Options:
  --change-cd    ask for the CD even if one is saved
  --forget-cd    delete the saved CD location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENRF_DRY_RUN=1)
  --help         this text
Anything after the CD goes to gasm-run, e.g. --param level=12 --param play=1, --mute.
OPENRF_CD=<CD> uses that CD for one run without saving it.

More: https://openrf.emdzej.pl/guide/gasm
"@
}

# $null if usable, else the reason
function Test-CD([string]$p) {
  if (Test-Path -LiteralPath $p -PathType Container) {
    if ((Test-Path -LiteralPath (Join-Path $p 'RFIRE.BIN') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $p 'ART\ART.CAR') -PathType Leaf)) { return $null }
    return "This folder does not look like the Return Fire CD (it needs RFIRE.BIN and ART\ART.CAR):`n$p"
  }
  if (Test-Path -LiteralPath $p -PathType Leaf) {
    switch ([IO.Path]::GetExtension($p).ToLowerInvariant()) {
      '.bin' { return $null }
      '.iso' { return $null }
      '.cue' { return 'A .cue sheet only names the image: choose the .bin file next to it.' }
      default { return "Not a disc image (.bin or .iso):`n$p" }
    }
  }
  return "The Return Fire CD was not found at:`n$p`nInsert or mount it, or choose it again."
}

function Get-Absolute([string]$p) {
  $full = [IO.Path]::GetFullPath([IO.Path]::Combine((Get-Location).ProviderPath, $p))
  if ($full.Length -gt 3) { $full = $full.TrimEnd('\') }   # keep D:\ as it is
  return $full
}

# A small dialog: CD folder / disc image / Quit; then the picker. Returns the path or $null.
function Request-CD([string]$message) {
  Add-Type -AssemblyName System.Windows.Forms
  [System.Windows.Forms.Application]::EnableVisualStyles()
  $form = New-Object System.Windows.Forms.Form
  $form.Text = $Title; $form.FormBorderStyle = 'FixedDialog'; $form.StartPosition = 'CenterScreen'
  $form.MaximizeBox = $false; $form.MinimizeBox = $false; $form.AutoSize = $true; $form.AutoSizeMode = 'GrowAndShrink'
  $form.Padding = New-Object System.Windows.Forms.Padding(12)
  $label = New-Object System.Windows.Forms.Label
  $label.Text = $message; $label.AutoSize = $true; $label.MaximumSize = New-Object System.Drawing.Size(460, 0)
  $label.Location = New-Object System.Drawing.Point(12, 12)
  $form.Controls.Add($label)
  $y = $label.PreferredHeight + 28
  $buttons = @(@('Choose CD folder...', 'folder'), @('Choose disc image...', 'image'), @('Quit', 'quit'))
  $x = 12
  foreach ($b in $buttons) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $b[0]; $btn.Tag = $b[1]; $btn.AutoSize = $true; $btn.Location = New-Object System.Drawing.Point($x, $y)
    $btn.Add_Click({ $form.Tag = $this.Tag; $form.Close() })
    $form.Controls.Add($btn); $x += 150
  }
  $form.AcceptButton = $form.Controls[1]; $form.CancelButton = $form.Controls[3]
  [void]$form.ShowDialog()
  switch ($form.Tag) {
    'folder' {
      $d = New-Object System.Windows.Forms.FolderBrowserDialog
      $d.Description = 'Choose the Return Fire CD (the drive or folder with RFIRE.BIN)'
      $d.ShowNewFolderButton = $false
      if ($d.ShowDialog() -eq 'OK') { return $d.SelectedPath }
    }
    'image' {
      $d = New-Object System.Windows.Forms.OpenFileDialog
      $d.Title = 'Choose the Return Fire disc image (.iso, or the .bin of a .bin/.cue pair)'
      $d.Filter = 'Disc images (*.bin;*.iso)|*.bin;*.iso|All files (*.*)|*.*'
      if ($d.ShowDialog() -eq 'OK') { return $d.FileName }
    }
  }
  return $null
}

$Intro = @"
OpenRF needs your Return Fire CD (it is not included).

Choose the CD itself or a mounted disc image (right-click an .iso, Mount: it gets a drive letter), a folder you copied the CD to, or a raw .bin or .iso image file.

Your choice is remembered; run OpenRF.cmd --change-cd to pick another.
"@

$rest = @($args)
while ($rest.Count -gt 0) {
  $a = [string]$rest[0]
  if ($a -eq '--help' -or $a -eq '-h' -or $a -eq '/?') { Show-Usage; exit 0 }
  elseif ($a -eq '--dry-run') { $Dry = $true }
  elseif ($a -eq '--change-cd') { $Change = $true }
  elseif ($a -eq '--forget-cd') { Remove-Item -LiteralPath $LocFile -ErrorAction SilentlyContinue; "forgot the CD location ($LocFile)"; exit 0 }
  else { break }
  $rest = @($rest | Select-Object -Skip 1)
}
$cd = $null; $save = $false
if ($rest.Count -gt 0 -and -not ([string]$rest[0]).StartsWith('-')) {
  $cd = Get-Absolute ([string]$rest[0]); $save = $true; $rest = @($rest | Select-Object -Skip 1)
}
if (-not $cd -and $env:OPENRF_CD) { $cd = Get-Absolute $env:OPENRF_CD }
if (-not $cd -and -not $Change -and (Test-Path -LiteralPath $LocFile)) {
  $cd = (Get-Content -LiteralPath $LocFile -TotalCount 1).Trim()
}

$msg = $Intro
if ($cd) { $msg = Test-CD $cd }
while (-not $cd -or (Test-CD $cd)) {
  if ($Dry -or $env:OPENRF_CD) {   # never open dialogs in test runs
    if ($cd) { [Console]::Error.WriteLine("no usable Return Fire CD: $msg") } else { [Console]::Error.WriteLine('no usable Return Fire CD') }
    exit 2
  }
  $cd = Request-CD $msg
  if (-not $cd) { exit 0 }
  $cd = Get-Absolute $cd; $save = $true
  $msg = Test-CD $cd
}
if ($save -and -not $Dry) {
  New-Item -ItemType Directory -Force -Path $ConfDir | Out-Null
  Set-Content -LiteralPath $LocFile -Value $cd -Encoding UTF8
}

if (Test-Path -LiteralPath $cd -PathType Container) { $data = @('--asset-dir', $cd) } else { $data = @('--asset', "cd=$cd") }
$run = Join-Path $Here 'gasm-run.exe'
$cmd = @((Join-Path $Here 'openrf.wasm')) + $data + @('--window', '1280x960') + $rest
if ($Dry) { (@($run) + $cmd | ForEach-Object { '"' + $_ + '"' }) -join ' '; exit 0 }
& $run @cmd
exit $LASTEXITCODE
