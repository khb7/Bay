// M1 — 모래 재질 에셋 생성 (SandMaterialBuilder.cpp)

#pragma once

#include "CoreMinimal.h"

namespace SandMaterial
{
	/** 기본 경로 — USandSurfaceComponent 가 재질이 비어 있으면 이것을 읽는다 */
	inline const TCHAR* DefaultPath() { return TEXT("/Game/Sand/M_Sand"); }

	/** 재질을 만들어 PackagePath(예: /Game/Sand/M_Sand)에 저장. 에디터 빌드 전용 */
	bool Build(const FString& PackagePath, FString& OutMessage);
}
