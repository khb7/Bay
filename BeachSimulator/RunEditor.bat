@echo off
rem BeachSimulator 에디터 실행. 엔진이 레지스트리에 등록돼 있지 않아 경로를 직접 지정한다.
start "" "D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0BeachSimulator.uproject"
