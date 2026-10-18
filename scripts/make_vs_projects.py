#!/usr/bin/env python3
"""Generates the Visual Studio 2026 solution (PodcastMP3.sln) and the four
.vcxproj files from one description, so they stay consistent.
Re-run after adding or removing source files."""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLSET = "v145"  # Visual Studio 2026

PROJECTS = [
    # name, folder, guid, type, sources, headers, rc, subsystem, extra libs, references
    dict(name="pmp3core", folder="src/core", guid="6F1C2A10-7B2D-4E8A-9C31-1A0D5E7F2B01", kind="StaticLibrary",
         sources=["util.cpp", "childproc.cpp", "settings.cpp", "media.cpp", "job.cpp", "host.cpp"],
         headers=["util.h", "childproc.h", "settings.h", "media.h", "job.h", "host.h", "version.h"],
         rc=None, subsystem=None, libs="", refs=[]),
    dict(name="podcastmp3-host", folder="src/host", guid="6F1C2A10-7B2D-4E8A-9C31-1A0D5E7F2B02", kind="Application",
         sources=["main.cpp"], headers=[], rc="host.rc", subsystem="Console",
         libs="shell32.lib;ole32.lib;advapi32.lib", refs=["pmp3core"]),
    dict(name="PodcastMP3", folder="src/gui", guid="6F1C2A10-7B2D-4E8A-9C31-1A0D5E7F2B03", kind="Application",
         sources=["main_win.cpp"], headers=["resource.h"], rc="gui.rc", subsystem="Windows",
         libs="comctl32.lib;uxtheme.lib;shell32.lib;ole32.lib;advapi32.lib", refs=["pmp3core"], postbuild=True),
    dict(name="pmp3tests", folder="tests", guid="6F1C2A10-7B2D-4E8A-9C31-1A0D5E7F2B04", kind="Application",
         sources=["test_core.cpp"], headers=[], rc=None, subsystem="Console",
         libs="shell32.lib;ole32.lib;advapi32.lib", refs=["pmp3core"]),
]

POSTBUILD = (
    'xcopy "$(SolutionDir)extension" "$(OutDir)extension\\" /E /I /Y /Q &gt;nul\r\n'
    'if exist "$(SolutionDir)packaging\\thirdparty\\ffmpeg.exe" xcopy "$(SolutionDir)packaging\\thirdparty\\*.exe" "$(OutDir)tools\\" /I /Y /Q &gt;nul\r\n'
    'if exist "$(SolutionDir)packaging\\thirdparty\\yt-dlp" xcopy "$(SolutionDir)packaging\\thirdparty\\yt-dlp" "$(OutDir)tools\\yt-dlp\\" /E /I /Y /Q &gt;nul\r\n'
    'exit /b 0'
)


def rel_to_root(folder):
    depth = len(folder.split("/"))
    return "\\".join([".."] * depth)


def vcxproj(p):
    up = rel_to_root(p["folder"])
    byname = {q["name"]: q for q in PROJECTS}
    lines = []
    a = lines.append
    a('<?xml version="1.0" encoding="utf-8"?>')
    a('<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    a('  <ItemGroup Label="ProjectConfigurations">')
    for c in ("Debug", "Release"):
        a(f'    <ProjectConfiguration Include="{c}|x64"><Configuration>{c}</Configuration><Platform>x64</Platform></ProjectConfiguration>')
    a('  </ItemGroup>')
    a('  <PropertyGroup Label="Globals">')
    a('    <VCProjectVersion>18.0</VCProjectVersion>')
    a(f'    <ProjectGuid>{{{p["guid"]}}}</ProjectGuid>')
    a(f'    <RootNamespace>{p["name"].replace("-", "_")}</RootNamespace>')
    a(f'    <ProjectName>{p["name"]}</ProjectName>')
    a('    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>')
    a('  </PropertyGroup>')
    a('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
    for c in ("Debug", "Release"):
        a(f'  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'{c}|x64\'" Label="Configuration">')
        a(f'    <ConfigurationType>{p["kind"]}</ConfigurationType>')
        a(f'    <UseDebugLibraries>{"true" if c == "Debug" else "false"}</UseDebugLibraries>')
        a(f'    <PlatformToolset>{TOOLSET}</PlatformToolset>')
        if c == "Release":
            a('    <WholeProgramOptimization>true</WholeProgramOptimization>')
        a('    <CharacterSet>Unicode</CharacterSet>')
        a('  </PropertyGroup>')
    a('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    a('  <PropertyGroup>')
    a('    <OutDir>$(SolutionDir)build\\vs\\$(Platform)\\$(Configuration)\\</OutDir>')
    a('    <IntDir>$(SolutionDir)build\\vs\\obj\\$(ProjectName)\\$(Platform)\\$(Configuration)\\</IntDir>')
    a('  </PropertyGroup>')
    for c in ("Debug", "Release"):
        a(f'  <ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'{c}|x64\'">')
        a('    <ClCompile>')
        a('      <WarningLevel>Level4</WarningLevel>')
        a('      <SDLCheck>true</SDLCheck>')
        a('      <ConformanceMode>true</ConformanceMode>')
        a('      <LanguageStandard>stdcpp17</LanguageStandard>')
        a('      <MultiProcessorCompilation>true</MultiProcessorCompilation>')
        a('      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>')
        a(f'      <AdditionalIncludeDirectories>$(SolutionDir)third_party;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>')
        defs = "UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN;NOMINMAX;_WIN32_WINNT=0x0A00;_CRT_SECURE_NO_WARNINGS"
        defs += ";_DEBUG" if c == "Debug" else ";NDEBUG"
        a(f'      <PreprocessorDefinitions>{defs};%(PreprocessorDefinitions)</PreprocessorDefinitions>')
        # Static runtime: the .exe files run on any Windows 10/11 PC without installing the VC++ runtime.
        a(f'      <RuntimeLibrary>{"MultiThreadedDebug" if c == "Debug" else "MultiThreaded"}</RuntimeLibrary>')
        if c == "Release":
            a('      <FunctionLevelLinking>true</FunctionLevelLinking>')
            a('      <IntrinsicFunctions>true</IntrinsicFunctions>')
        a('    </ClCompile>')
        if p["subsystem"]:
            a('    <Link>')
            a(f'      <SubSystem>{p["subsystem"]}</SubSystem>')
            a('      <GenerateDebugInformation>true</GenerateDebugInformation>')
            # The .rc file already embeds app.manifest (common controls v6, DPI awareness).
            a('      <GenerateManifest>false</GenerateManifest>')
            a(f'      <AdditionalDependencies>{p["libs"]};%(AdditionalDependencies)</AdditionalDependencies>')
            if c == "Release":
                a('      <EnableCOMDATFolding>true</EnableCOMDATFolding>')
                a('      <OptimizeReferences>true</OptimizeReferences>')
            a('    </Link>')
        if p.get("postbuild"):
            a('    <PostBuildEvent>')
            a('      <Message>Copying extension and bundled tools next to the program</Message>')
            a(f'      <Command>{POSTBUILD}</Command>')
            a('    </PostBuildEvent>')
        a('  </ItemDefinitionGroup>')
    a('  <ItemGroup>')
    for s in p["sources"]:
        a(f'    <ClCompile Include="{s}" />')
    a('  </ItemGroup>')
    if p["headers"]:
        a('  <ItemGroup>')
        for h in p["headers"]:
            a(f'    <ClInclude Include="{h}" />')
        a('  </ItemGroup>')
    if p["rc"]:
        a('  <ItemGroup>')
        a(f'    <ResourceCompile Include="{p["rc"]}" />')
        a('  </ItemGroup>')
    if p["refs"]:
        a('  <ItemGroup>')
        for r in p["refs"]:
            q = byname[r]
            relpath = os.path.relpath(os.path.join(q["folder"], q["name"] + ".vcxproj"), p["folder"]).replace("/", "\\")
            a(f'    <ProjectReference Include="{relpath}">')
            a(f'      <Project>{{{q["guid"]}}}</Project>')
            a('    </ProjectReference>')
        a('  </ItemGroup>')
    a('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    a('</Project>')
    return "\r\n".join(lines) + "\r\n"


def solution():
    cpp = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"
    L = ["", "Microsoft Visual Studio Solution File, Format Version 12.00", "# Visual Studio Version 18",
         "VisualStudioVersion = 18.0.0.0", "MinimumVisualStudioVersion = 10.0.40219.1"]
    for p in PROJECTS:
        path = os.path.join(p["folder"], p["name"] + ".vcxproj").replace("/", "\\")
        L.append(f'Project("{{{cpp}}}") = "{p["name"]}", "{path}", "{{{p["guid"]}}}"')
        L.append("EndProject")
    L.append("Global")
    L.append("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    for c in ("Debug", "Release"):
        L.append(f"\t\t{c}|x64 = {c}|x64")
    L.append("\tEndGlobalSection")
    L.append("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    for p in PROJECTS:
        for c in ("Debug", "Release"):
            L.append(f"\t\t{{{p['guid']}}}.{c}|x64.ActiveCfg = {c}|x64")
            L.append(f"\t\t{{{p['guid']}}}.{c}|x64.Build.0 = {c}|x64")
    L.append("\tEndGlobalSection")
    L.append("\tGlobalSection(SolutionProperties) = preSolution")
    L.append("\t\tHideSolutionNode = FALSE")
    L.append("\tEndGlobalSection")
    L.append("EndGlobal")
    return "\r\n".join(L) + "\r\n"


def main():
    for p in PROJECTS:
        with open(os.path.join(ROOT, p["folder"], p["name"] + ".vcxproj"), "w", newline="") as f:
            f.write(vcxproj(p))
    with open(os.path.join(ROOT, "PodcastMP3.sln"), "w", newline="") as f:
        f.write("﻿" + solution())
    print("wrote PodcastMP3.sln and", len(PROJECTS), "projects")


if __name__ == "__main__":
    main()
