// Fill out your copyright notice in the Description page of Project Settings.


#include "MiniGame/StockGraphWidget.h"
#include "Rendering/DrawElements.h"
#include "TimerManager.h"

void UStockGraphWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 기존 그래프 데이터 초기화
	GraphPoint.Empty();
}

void UStockGraphWidget::NativeDestruct()
{
	// 위젯이 제거될 때 Timer 정리
	StopStockGraph();

	Super::NativeDestruct();
}

void UStockGraphWidget::StartStockGraph(float InStopMultiplier)
{
	// 이전에 실행 중인 그래프와 Timer 정리
	StopStockGraph();

	// 기존 그래프 초기화
	ResetStockGraph();

	// 그래프 시작 위치 설정
	CurrentX = 100.0f;
	CurrentY = 600.0f;

	// 배율 및 경과 시간 초기화
	CurrentMultiplier = MinStopMultiplier;
	ElapsedTime = 0.0f;

	// StockMachine에서 전달받은 배율 저장
	StopMultiplier = FMath::Clamp(
		FMath::RoundToInt(InStopMultiplier * 100.0f) / 100.0f, 
		MinStopMultiplier, 
		MaxStopMultiplier
	);

	// 첫번째 그래프 좌표 추가
	AddGraph(FVector2D(CurrentX, CurrentY));

	// 배율이 1.00x가 나온 경우 그래프가 진행하지 않고 즉시 종료
	if (StopMultiplier <= MinStopMultiplier)
	{
		CurrentMultiplier = MinStopMultiplier;
		GraphRunning = false;
		return;
	}

	// 그래프 진행 상태 활성화
	GraphRunning = true;

	// 일정한 주기로 그래프 갱신
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			GraphTimerHandle, this, &UStockGraphWidget::UpdateStockGraph, UpdateInterval, true
		);
	}
}

void UStockGraphWidget::StopStockGraph()
{
	// 그래프 진행 정지
	GraphRunning = false;

	// 실행 중인 Timer 제거
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(GraphTimerHandle);
	}
}
void UStockGraphWidget::UpdateStockGraph()
{
	// 그래프가 진행 중이 아니면 갱신하지 않음.
	if (!GraphRunning)
	{
		return;
	}
	
	// 잘못된 설정값으로 인한 0 나누기 방지
	if (GraphDuration <= 0.0f ||
		MaxStopMultiplier <= MinStopMultiplier)
	{
		StopStockGraph();
		return;
	}

	// 그래프 경과 시간 증가
	ElapsedTime += UpdateInterval;

	// 전체 그래프 진행률
	// GraphDuration 동안 0.0 → 1.0으로 증가
	const float Progress = FMath::Clamp(ElapsedTime / GraphDuration, 0.0f, 1.0f);

	// 기본적으로 조금씩 상승
	const float PowerCurve = FMath::Pow(Progress, CurvePower);

	// 그래프를 후반에 급격하게 상승시키기 위한 곡선
	const float CurveAlpha = FMath::Lerp(Progress, PowerCurve, 0.65f);

	// 모든 라운드에서 동일한 X 진행
	CurrentX = FMath::Lerp(100.0f, MaxX, Progress);

	// 모든 라운드에서 동일한 1배 → 5배 상승 곡선 계산
	const float CalculateMultiplier = FMath::Lerp(MinStopMultiplier, MaxStopMultiplier, CurveAlpha);

	// 이번 라운드의 랜덤 종료 배율까지만 허용
	CurrentMultiplier = FMath::Min(CalculateMultiplier, StopMultiplier);

	// 현재 배율을 0~1 범위로 변환
	const float NormalizedMultiplier =
		(CurrentMultiplier - MinStopMultiplier) /
		(MaxStopMultiplier - MinStopMultiplier);

	// 배율에 맞춰 Y 위치 결정
	CurrentY = FMath::Lerp(600.0f, 120.0f, NormalizedMultiplier);

	// 현재 그래프 좌표 추가
	AddGraph(FVector2D(CurrentX, CurrentY));

	// 이번 판의 랜덤 종료 배율에 도달하면 정지
	if (CurrentMultiplier >= StopMultiplier)
	{
		CurrentMultiplier = StopMultiplier;
		StopStockGraph();
		return;
	}
}
void UStockGraphWidget::AddGraph(FVector2D NewPoint)
{
	// 새로운 그래프 좌표 추가
	GraphPoint.Add(NewPoint);

	// 그래프 변경 내용을 다시 그리도록 갱신
	InvalidateLayoutAndVolatility();
}

void UStockGraphWidget::ResetStockGraph()
{
	// 모든 그래프 좌표 삭제
	GraphPoint.Empty();

	// 초기화된 상태를 화면에 반영
	InvalidateLayoutAndVolatility();
}

int32 UStockGraphWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool ParentEnabled) const
{
	// 부모 위젯의 기본 Paint 처리
	const int32 SuperLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, ParentEnabled
	);

	// 점이 2개 미만이면 선을 만들 수 없기에 종료
	if (GraphPoint.Num() < 2)
	{
		return SuperLayer;
	}

	// Slate에서 사용할 FVector2f 배열 생성
	TArray<FVector2f> SlatePoint;
	SlatePoint.Reserve(GraphPoint.Num());

	// FVector2D 좌표를 Slate용 FVector2f로 변환
	for (const FVector2D& Point:GraphPoint)
	{
		SlatePoint.Add(FVector2f(static_cast<float>(Point.X), static_cast<float>(Point.Y)));
	}

	// 부모 위젯보다 한 단계 위 레이어에 그래프 출력
	const int32 GraphLayer = SuperLayer + 1;

	// 저장된 좌표들을 연결하여 하나의 그래프 선으로 출력
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		GraphLayer,
		AllottedGeometry.ToPaintGeometry(),
		SlatePoint,
		ESlateDrawEffect::None,
		FLinearColor::White,
		true,
		5.0f
	);

	return GraphLayer;
}