// 스윕 실행기. 에디터 없이 돈다.
//
//   UnrealEditor-Cmd.exe <uproject> -run=SpikeSweep [-particles=N] [-frames=N]
//                        [-mu=0.7,1.0,1.3] [-substeps=5,10,20] [-iterations=1,2,4] [-nocsv]

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SpikeSweepCommandlet.generated.h"

UCLASS()
class USpikeSweepCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USpikeSweepCommandlet();

	virtual int32 Main(const FString& Params) override;
};
