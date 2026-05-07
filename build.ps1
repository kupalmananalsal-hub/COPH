param(
    [ValidateSet("all", "cli", "winapp")]
    [string]$Target = "all"
)

$ErrorActionPreference = "Stop"

function Require-Gpp {
    $gpp = Get-Command g++ -ErrorAction SilentlyContinue
    if (-not $gpp) {
        throw "g++ not found in PATH. Install MinGW-w64 and reopen terminal."
    }
}

function Build-Cli {
    Write-Host "Building CLI app..."
    & g++ "ev_range_planner_cli.cpp" -std=gnu++11 -O2 -o "ev_range_planner_cli.exe"
    if ($LASTEXITCODE -ne 0) {
        throw "CLI build failed."
    }
    Write-Host "Built ev_range_planner_cli.exe"
}

function Build-WinApp {
    Write-Host "Building Win32 app..."
    & g++ "ev_range_planner_winapp.cpp" -std=gnu++11 -O2 -mwindows -lgdi32 -luser32 -o "EVPlanner.exe"
    if ($LASTEXITCODE -ne 0) {
        throw "Win app build failed."
    }
    Write-Host "Built EVPlanner.exe"
}

try {
    Require-Gpp

    switch ($Target) {
        "cli" {
            Build-Cli
        }
        "winapp" {
            Build-WinApp
        }
        default {
            Build-Cli
            Build-WinApp
        }
    }

    Write-Host ""
    Write-Host "Build completed successfully."
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
