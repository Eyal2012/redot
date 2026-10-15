param(
  [Parameter(Mandatory=$true)][string]$GoFile,
  [Parameter(Mandatory=$true)][string]$StopFile,
  [Parameter(Mandatory=$true)][string]$BeepPath,
  [int]$DurationSeconds = 75
, [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Preloaded audio and paint observations help separate source scheduling from
# PandaClip mapping. SoundPlayer does not expose speaker presentation QPC.
Add-Type -TypeDefinition @'
using System; using System.IO;
public static class TimedWave {
 public static void Write(string path,int seconds) {
  int rate=48000; byte[] wave=new byte[44+seconds*rate*4];
  using(var stream=new MemoryStream(wave)) using(var writer=new BinaryWriter(stream)) {
   writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));writer.Write(wave.Length-8);
   writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));writer.Write(16);writer.Write((short)1);writer.Write((short)2);
   writer.Write(rate);writer.Write(rate*4);writer.Write((short)4);writer.Write((short)16);
   writer.Write(System.Text.Encoding.ASCII.GetBytes("data"));writer.Write(wave.Length-44);
  }
  for(int cue=3;cue<seconds-2;cue+=8) for(int n=0;n<8640;n++) {
   double env=Math.Min(1.0,Math.Min(n/240.0,(8640-n)/240.0));
   short v=(short)(Math.Sin(2*Math.PI*1000*n/rate)*14000*env);
   int p=44+(cue*rate+n)*4;wave[p]=(byte)v;wave[p+1]=(byte)(v>>8);wave[p+2]=(byte)v;wave[p+3]=(byte)(v>>8);
  }
  File.WriteAllBytes(path,wave);
 }
}
'@
[TimedWave]::Write($BeepPath,$DurationSeconds)
$script:events = @(for($t=3;$t -lt $DurationSeconds-2;$t+=8){$t})
$script:lastFlash=$false
$sourceLog=$BeepPath + '.source.csv'
'event,scheduled_seconds,paint_seconds,qpc_ticks' | Set-Content -LiteralPath $sourceLog

$player = [System.Media.SoundPlayer]::new($BeepPath)
$player.Load()
$form = [System.Windows.Forms.Form]::new()
$form.Text = 'PandaClip Timed Flash and Beep Test'
$form.ClientSize = [Drawing.Size]::new(960, 540)
$form.StartPosition = 'CenterScreen'
$form.BackColor = [Drawing.Color]::Black
$script:clock = $null
$script:frame = 0
$timer = [System.Windows.Forms.Timer]::new()
$timer.Interval = 16
$timer.Add_Tick({
  if (-not $script:clock -and (Test-Path -LiteralPath $GoFile)) {
    $script:clock = [Diagnostics.Stopwatch]::StartNew()
    $player.Play()
    Add-Content -LiteralPath $sourceLog -Value ('play_request,0,'+$script:clock.Elapsed.TotalSeconds+','+[Diagnostics.Stopwatch]::GetTimestamp())
  }
  $script:frame++
  $form.Invalidate()
  if (Test-Path -LiteralPath $StopFile) { $form.Close() }
})
$form.Add_Paint({ param($sender, $e)
  $g = $e.Graphics
  $elapsed = if ($script:clock) { $script:clock.Elapsed.TotalSeconds } else { -1.0 }
  $isFlash = $false
  foreach ($eventTime in $script:events) {
    if ($elapsed -ge $eventTime -and $elapsed -lt ($eventTime + 0.24)) { $isFlash = $true; break }
  }
  if($isFlash -and !$script:lastFlash) { Add-Content -LiteralPath $sourceLog -Value ('paint,'+$eventTime+','+$elapsed+','+[Diagnostics.Stopwatch]::GetTimestamp()) }
  $script:lastFlash=$isFlash
  $g.Clear($(if ($isFlash) { [Drawing.Color]::White } else { [Drawing.Color]::FromArgb(16, 20, 28) }))
  $x = 40 + (($script:frame * 9) % 840)
  $g.FillEllipse([Drawing.Brushes]::DeepSkyBlue, $x, 440, 32, 32)
  $font = [Drawing.Font]::new('Segoe UI', 16)
  $label = if ($script:clock) { 'White flash + 1 kHz beep every 8 seconds (preloaded audio)' } else { 'Waiting for replay buffer warm-up...' }
  $g.DrawString($label, $font, [Drawing.Brushes]::Lime, 20, 18)
  $font.Dispose()
})
$form.Add_Shown({ $timer.Start() })
try { [Windows.Forms.Application]::Run($form) }
finally { $timer.Stop(); $timer.Dispose(); $player.Stop(); $player.Dispose(); $form.Dispose() }
