param(
    [Parameter(Position=0)]
    [ValidateSet('Env','Fast','Integration','E2E','All')]
    [string]$Suite = 'Fast',
    [ValidateSet('Debug','Release','Both')]
    [string]$Configuration = 'Both',
    [int]$Jobs = 2,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ExtraArgs
)
$ErrorActionPreference = 'Stop'
$python = Get-Command python -ErrorAction SilentlyContinue
if ($python -and $python.Source -notlike '*\WindowsApps\*') {
    & $python.Source "$PSScriptRoot/check.py" $Suite.ToLowerInvariant() --configuration $Configuration --jobs $Jobs @ExtraArgs
} else {
    $launcher = Get-Command py -ErrorAction SilentlyContinue
    if (!$launcher) { [Console]::Error.WriteLine('Python 3.10+ is required. Install Python, then rerun check-env.'); exit 2 }
    & $launcher.Source -3 "$PSScriptRoot/check.py" $Suite.ToLowerInvariant() --configuration $Configuration --jobs $Jobs @ExtraArgs
}
exit $LASTEXITCODE
