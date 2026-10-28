[CmdletBinding()]
param(
    [ValidateSet("all", "run", "check", "shaders", "clean", "info")]
    [string]$Target = "all",

    [string]$Example = "01_window",

    [string]$Model = "assets/models/african_head.obj",

    [string]$Compiler = "gcc",

    [string]$VulkanSdk = $(if ($env:VULKAN_SDK) {
        $env:VULKAN_SDK
    } else {
        "C:\VulkanSDK\1.4.363.0"
    }),

    [string]$GlfwRoot = $(if ($env:GLFW_ROOT) {
        $env:GLFW_ROOT
    } else {
        "C:\vcpkg\installed\x64-windows"
    })
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$BuildDirectory = Join-Path $Root "build"
$ExampleDirectory = Join-Path $Root "examples/$Example"
$ShaderDirectory = Join-Path $Root "shaders"
$ShaderOutputDirectory = Join-Path $BuildDirectory "shaders"
$Executable = Join-Path $BuildDirectory "$Example.exe"
$IncludeDirectory = Join-Path $VulkanSdk "Include"
$LibraryDirectory = Join-Path $VulkanSdk "Lib"
$GlfwIncludeDirectory = Join-Path $GlfwRoot "include"
$GlfwLibraryDirectory = Join-Path $GlfwRoot "lib"
$GlfwRuntime = Join-Path $GlfwRoot "bin/glfw3.dll"
$Glslc = Get-Command glslc -ErrorAction Stop
$Gcc = Get-Command $Compiler -ErrorAction Stop
$ModelPath = if ([IO.Path]::IsPathRooted($Model)) {
    $Model
} else {
    Join-Path $Root $Model
}

function Show-Info {
    Write-Host "Compiler:     $($Gcc.Source)"
    & $Gcc.Source --version | Select-Object -First 1
    Write-Host "Shader tool:  $($Glslc.Source)"
    & $Glslc.Source --version | Select-Object -First 1
    Write-Host "Vulkan SDK:   $VulkanSdk"
    Write-Host "GLFW root:    $GlfwRoot"
    Write-Host "Example:      $Example"
    Write-Host "Executable:   $Executable"
}

function Build-Shaders {
    New-Item -ItemType Directory -Force -Path $ShaderOutputDirectory | Out-Null
    Get-ChildItem -LiteralPath $ShaderDirectory -File |
        Where-Object { $_.Extension -in @(".vert", ".frag") } |
        ForEach-Object {
            $stage = $_.Extension.TrimStart(".")
            $output = Join-Path $ShaderOutputDirectory "$($_.Name).spv"
            & $Glslc.Source "-fshader-stage=$stage" $_.FullName -o $output
            if ($LASTEXITCODE -ne 0) {
                throw "Shader compilation failed: $($_.Name)"
            }
        }
}

function Build-Example {
    if (-not (Test-Path -LiteralPath $IncludeDirectory)) {
        throw "Vulkan headers not found at '$IncludeDirectory'. Set -VulkanSdk."
    }
    if (-not (Test-Path -LiteralPath $LibraryDirectory)) {
        throw "Vulkan import library directory not found at '$LibraryDirectory'."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $GlfwIncludeDirectory "GLFW/glfw3.h"))) {
        throw "GLFW headers not found at '$GlfwIncludeDirectory'. Set -GlfwRoot."
    }
    if (-not (Test-Path -LiteralPath $GlfwLibraryDirectory)) {
        throw "GLFW library directory not found at '$GlfwLibraryDirectory'."
    }

    $sources = @()
    $sources += Get-ChildItem -LiteralPath $ExampleDirectory -Filter "*.c" -File |
        ForEach-Object { $_.FullName }
    $sources += Join-Path $Root "src/vk_helpers.c"

    New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
    $arguments = @(
        "-std=c11",
        "-O0",
        "-g3",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wshadow",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wformat=2",
        "-Wundef",
        "-Werror",
        "-I$(Join-Path $Root 'src')",
        "-I$IncludeDirectory",
        "-I$GlfwIncludeDirectory"
    ) + $sources + @(
        "-L$LibraryDirectory",
        "-L$GlfwLibraryDirectory",
        "-lvulkan-1",
        "-lglfw3dll",
        "-o$Executable"
    )

    & $Gcc.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "C compilation failed."
    }

    if (-not (Test-Path -LiteralPath $GlfwRuntime)) {
        throw "GLFW runtime DLL not found at '$GlfwRuntime'."
    }
    Copy-Item -LiteralPath $GlfwRuntime `
        -Destination (Join-Path $BuildDirectory "glfw3.dll") `
        -Force
}

function Run-Check {
    & $Executable --smoke-test --model $ModelPath
    if ($LASTEXITCODE -ne 0) {
        throw "The example exited with code $LASTEXITCODE."
    }
}

function Remove-BuildOutput {
    $resolvedBuild = [IO.Path]::GetFullPath($BuildDirectory)
    $resolvedRoot = [IO.Path]::GetFullPath($Root)
    $expectedPrefix = $resolvedRoot.TrimEnd(
        [IO.Path]::DirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar

    if (-not $resolvedBuild.StartsWith(
        $expectedPrefix,
        [StringComparison]::OrdinalIgnoreCase
    ) -or (Split-Path $resolvedBuild -Leaf) -ne "build") {
        throw "Refusing to remove unexpected build path: $resolvedBuild"
    }

    if (Test-Path -LiteralPath $resolvedBuild) {
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
}

switch ($Target) {
    "all" {
        Build-Example
        Build-Shaders
    }
    "run" {
        Build-Example
        Build-Shaders
        & $Executable --model $ModelPath
        exit $LASTEXITCODE
    }
    "check" {
        Build-Example
        Build-Shaders
        Run-Check
    }
    "shaders" {
        Build-Shaders
    }
    "clean" {
        Remove-BuildOutput
    }
    "info" {
        Show-Info
    }
}
