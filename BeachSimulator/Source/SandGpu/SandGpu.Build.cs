using UnrealBuildTool;

// M0-b GPU 솔버. 전역 셰이더는 엔진 초기화 전에 등록돼야 해서 게임 모듈(Default)과 따로 PostConfigInit 로 올린다.
public class SandGpu : ModuleRules
{
	public SandGpu(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "RenderCore", "RHI", "Projects" });
	}
}
