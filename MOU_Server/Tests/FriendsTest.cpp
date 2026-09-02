// M2 검증 - 친구 관계와 메신저 저장소의 회귀 테스트.
//
// [왜 이 테스트가 있는가]
//   CHAT_DESIGN.md 12절이 M2 를 위험 구간으로 꼽은 이유가 두 가지인데,
//   둘 다 **깨져도 컴파일은 통과하고 증상만 이상해지는** 종류다:
//
//     1. low/high 정규화가 한 군데로 모여 있는가
//        -> 어긋나면 관계가 두 줄로 저장돼 "끊었는데 목록에 남는다"
//     2. Respond 의 방향 검사가 살아 있는가
//        -> 빠지면 **자기가 보낸 신청을 스스로 수락**해 상대 동의 없이 친구가 된다
//
//   6~8번 항목이 1번을, 4번 항목이 2번을 지킨다. 스키마나 SQL 을 건드릴 때
//   이 파일을 같이 돌린다.
//
// [실행]
//   cmake --build . --target FriendsTest
//   ./FriendsTest        (현재 디렉터리에 m2.db 를 만들었다 지운다)
//
//   종료 코드 0 = 전부 통과. CI 에 걸 때 그대로 쓸 수 있다.
#include "Accounts.h"
#include "DirectMessages.h"
#include "Friends.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace MOU;

static int GFail = 0;

static void Check(bool Cond, const char* What)
{
    std::printf("  %s %s\n", Cond ? "[OK]" : "[FAIL]", What);
    if (!Cond) { ++GFail; }
}

static const char* StateName(EFriendState S)
{
    switch (S)
    {
    case EFriendState::Friend:          return "Friend";
    case EFriendState::PendingOutgoing: return "PendingOutgoing";
    case EFriendState::PendingIncoming: return "PendingIncoming";
    }
    return "?";
}

static uint64_t MakeAccount(const char* Id, const char* Nick)
{
    uint64_t Uid = 0;
    const EAccountResult R = Accounts::Create(Id, "password123", Nick, Uid);
    if (R != EAccountResult::Success)
    {
        std::printf("  [FAIL] 계정 생성 실패: %s\n", Id);
        ++GFail;
    }
    return Uid;
}

int main()
{
    std::remove("m2.db");
    std::remove("m2.db-wal");
    std::remove("m2.db-shm");

    if (!Accounts::Start("m2.db") || !Friends::Start("m2.db") || !DirectMessages::Start("m2.db"))
    {
        std::printf("DB 열기 실패\n");
        return 1;
    }

    std::printf("\n--- 계정 준비 ---\n");
    const uint64_t A = MakeAccount("aaa", "kimta");
    const uint64_t B = MakeAccount("bbb", "averty");
    const uint64_t C = MakeAccount("ccc", "hazzys");
    // 동명이인. AmbiguousName 을 만들기 위한 계정이다.
    const uint64_t D = MakeAccount("ddd", "kimta");
    std::printf("  A=%llu B=%llu C=%llu D=%llu\n",
        (unsigned long long)A, (unsigned long long)B,
        (unsigned long long)C, (unsigned long long)D);

    uint64_t Target = 0; std::string Nick; bool bBecame = false;

    std::printf("\n--- 1. 신청 (A -> B) ---\n");
    Check(Friends::Add(A, "averty", Target, Nick, bBecame) == EFriendResult::Success, "신청 성공");
    Check(Target == B && Nick == "averty", "상대 해석 정확");
    Check(!bBecame, "아직 친구는 아니다");

    std::printf("\n--- 2. 중복/자기신청/동명이인 거절 ---\n");
    Check(Friends::Add(A, "averty", Target, Nick, bBecame) == EFriendResult::AlreadyPending, "중복 신청 거절");
    Check(Friends::Add(A, "kimta",  Target, Nick, bBecame) == EFriendResult::AmbiguousName,  "동명이인 -> AmbiguousName");
    Check(Friends::Add(A, "nobody", Target, Nick, bBecame) == EFriendResult::NotFound,       "없는 닉 -> NotFound");
    Check(Friends::Add(A, "kim#1",  Target, Nick, bBecame) == EFriendResult::InvalidFormat,  "'#' 은 아직 InvalidFormat");
    // 대소문자 무시 확인
    Check(Friends::Add(B, "AVERTY", Target, Nick, bBecame) == EFriendResult::SelfRequest,    "대소문자 무시 + 자기신청 거절");

    std::printf("\n--- 3. 방향 확인 (목록) ---\n");
    std::vector<FriendRow> ListA, ListB;
    Friends::GetList(A, ListA);
    Friends::GetList(B, ListB);
    Check(ListA.size() == 1 && ListA[0].State == EFriendState::PendingOutgoing, "A 쪽은 PendingOutgoing");
    Check(ListB.size() == 1 && ListB[0].State == EFriendState::PendingIncoming, "B 쪽은 PendingIncoming");
    for (const auto& R : ListA) std::printf("    A의목록: %s (%s)\n", R.Nickname.c_str(), StateName(R.State));
    for (const auto& R : ListB) std::printf("    B의목록: %s (%s)\n", R.Nickname.c_str(), StateName(R.State));

    std::printf("\n--- 4. ★ 자기가 보낸 신청을 스스로 수락 시도 (막혀야 한다) ---\n");
    Check(Friends::Respond(A, A, true) != EFriendResult::Success, "A 가 자기 신청 수락 -> 거부");
    Check(Friends::Respond(A, B, true) != EFriendResult::Success, "A 가 B 발신인 척 수락 -> 거부(방향 불일치)");
    Check(!Friends::AreFriends(A, B), "아직 친구 아님이 유지됨");

    std::printf("\n--- 5. 수락 ---\n");
    Check(Friends::Respond(B, A, true) == EFriendResult::Success, "B 가 수락");
    Check(Friends::AreFriends(A, B) && Friends::AreFriends(B, A), "양방향 친구 성립");
    Friends::GetList(A, ListA);
    Check(ListA.size() == 1 && ListA[0].State == EFriendState::Friend, "목록이 Friend 로 바뀜");

    std::printf("\n--- 6. 맞신청 즉시 친구 (B->C, C->B) ---\n");
    Check(Friends::Add(B, "hazzys", Target, Nick, bBecame) == EFriendResult::Success && !bBecame, "B->C 신청");
    Check(Friends::Add(C, "averty", Target, Nick, bBecame) == EFriendResult::Success && bBecame,  "C->B 맞신청 -> 즉시 친구");
    Check(Friends::AreFriends(B, C), "B,C 친구 성립");

    std::printf("\n--- 7. GetFriendIds (수락된 것만) ---\n");
    std::vector<uint64_t> Ids;
    Friends::GetFriendIds(B, Ids);
    Check(Ids.size() == 2, "B 의 친구 2명 (A, C)");
    Friends::GetFriendIds(A, Ids);
    Check(Ids.size() == 1 && Ids[0] == B, "A 의 친구는 B 하나");

    std::printf("\n--- 8. low/high 순서 무관 확인 ---\n");
    // A<B 든 B<A 든 같은 한 줄이어야 한다.
    Check(Friends::Add(B, "kimta", Target, Nick, bBecame) == EFriendResult::AmbiguousName,
          "역방향에서도 동명이인 판정 동일");

    std::printf("\n--- 9. DM 저장 / 조회 / 안읽음 ---\n");
    uint64_t Mid = 0; int64_t Ts = 0;
    Check(DirectMessages::Send(A, B, "\xEC\x95\x88\xEB\x85\x95", 6, Mid, Ts), "A->B 전송(한글)");
    Check(Mid > 0 && Ts > 0, "MessageId/Timestamp 채워짐");
    uint64_t Mid2 = 0;
    DirectMessages::Send(B, A, "hi", 2, Mid2, Ts);
    DirectMessages::Send(A, B, "zzz", 3, Mid2, Ts);

    std::vector<DmRow> Hist; bool bMore = false;
    Check(DirectMessages::GetHistory(A, B, 0, 50, Hist, bMore), "기록 조회");
    Check(Hist.size() == 3, "3통 다 보임 (양방향 합쳐서)");
    Check(!bMore, "더 없음");
    Check(Hist[0].MessageId < Hist[2].MessageId, "오래된 것 -> 최신 순서");
    Check(Hist[0].Text == "\xEC\x95\x88\xEB\x85\x95", "한글 본문 왕복 무손실");
    for (const auto& R : Hist)
        std::printf("    #%llu from=%llu \"%s\"\n",
            (unsigned long long)R.MessageId, (unsigned long long)R.FromUserId, R.Text.c_str());

    std::printf("\n--- 10. 커서 페이징 ---\n");
    std::vector<DmRow> Page;
    Check(DirectMessages::GetHistory(A, B, 0, 2, Page, bMore), "2개만 요청");
    Check(Page.size() == 2 && bMore, "2개 + 더있음 플래그");
    Check(DirectMessages::GetHistory(A, B, Page[0].MessageId, 2, Page, bMore), "커서로 이전 페이지");
    Check(Page.size() == 1 && !bMore, "남은 1개, 더없음");

    std::printf("\n--- 11. 안읽음 개수 / 읽음 처리 ---\n");
    std::vector<UnreadCount> Unread;
    DirectMessages::GetUnreadCounts(B, Unread);
    Check(Unread.size() == 1 && Unread[0].PeerUserId == A && Unread[0].Count == 2, "B 는 A 에게서 2통 안읽음");
    std::vector<DmRow> Pending;
    DirectMessages::GetPending(B, 100, Pending);
    Check(Pending.size() == 2, "밀린 메시지 2통");
    Check(DirectMessages::MarkRead(B, A), "읽음 처리");
    DirectMessages::GetUnreadCounts(B, Unread);
    Check(Unread.empty(), "읽고 나면 배지 없음");
    DirectMessages::GetUnreadCounts(A, Unread);
    Check(Unread.size() == 1 && Unread[0].Count == 1, "A 쪽 안읽음은 그대로 1통");

    std::printf("\n--- 12. 친구 삭제 ---\n");
    Check(Friends::Remove(A, B) == EFriendResult::Success, "삭제");
    Check(!Friends::AreFriends(A, B), "친구 아님");
    Friends::GetList(A, ListA);
    Check(ListA.empty(), "목록에서 사라짐");
    Check(DirectMessages::GetHistory(A, B, 0, 50, Hist, bMore) && Hist.size() == 3,
          "★ 친구를 끊어도 대화 기록은 남는다");

    Friends::Stop();
    DirectMessages::Stop();
    Accounts::Stop();

    std::printf("\n=========================\n");
    std::printf(GFail == 0 ? "  전부 통과\n" : "  실패 %d건\n", GFail);
    std::printf("=========================\n");
    return GFail == 0 ? 0 : 1;
}
