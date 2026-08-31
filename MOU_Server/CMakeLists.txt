cmake_minimum_required(VERSION 3.16)
# sqlite3 앰알가메이션이 C 파일이라 C 도 켜야 한다 (CXX 만 두면 설정 단계에서 실패).
project(MOU_Server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /utf-8 : 소스에 한글 문자열이 있으므로 필수
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Threads REQUIRED)

# 서버와 클라이언트가 같은 프레이밍 코드를 쓴다.
add_library(mou_shared STATIC
    Shared/Framing.cpp
)
target_include_directories(mou_shared PUBLIC Shared)
target_link_libraries(mou_shared PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(mou_shared PUBLIC ws2_32)
endif()

# SQLite (6단계 채팅 로그 영속화).
# ThirdParty/sqlite/ 는 언리얼 5.8 이 동봉한 앰알가메이션 3.47.1 을 그대로 복사한 것이다.
# 퍼블릭 도메인이라 저장소에 넣어도 문제없고, 이렇게 두면 언리얼 설치 경로가
# PC 마다 달라도(F: / C: 등) 서버만 따로 빌드할 수 있다.
add_library(sqlite3 STATIC ThirdParty/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ThirdParty/sqlite)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1          # 커넥션은 라이터 스레드 전용이지만 안전하게 직렬화 모드
    SQLITE_OMIT_LOAD_EXTENSION   # 외부 .dll 확장을 로드할 일이 없다. 공격면 축소
    SQLITE_DQS=0                 # 큰따옴표를 문자열로 봐주는 옛 관용을 끈다(오타를 에러로)
    SQLITE_DEFAULT_MEMSTATUS=0   # 메모리 통계 미사용. 약간 빨라진다
)
if(MSVC)
    # 남의 코드라 우리 /W4 경고를 볼 이유가 없다. 우리 코드 경고에 묻히지 않게 끈다.
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

add_executable(Server
    Server/Server.cpp
    Server/ChatLog.cpp
    Server/Accounts.cpp
    Server/Crypto.cpp
    Server/Rooms.cpp
    Server/Session.cpp
    # v7 친구 + 메신저. 둘 다 Accounts 와 같은 동기 SQLite 다
    # (ChatLog 의 비동기 큐와 다른 이유는 각 헤더 주석 참고).
    Server/Friends.cpp
    Server/DirectMessages.cpp
    # 공유기(UPnP)에 자기 리슨 포트를 열어달라고 요청한다. --upnp 로만 동작한다.
    # 언리얼 쪽에도 같은 이름의 파일이 있지만 여는 포트가 달라서 별개다
    # (여기는 Server.exe 의 9000/TCP, 저기는 리슨서버의 7777/UDP).
    Server/NatPortMapping.cpp
    # 직접 P2P가 성립하지 않을 때만 쓰는, 게임 패킷 비해석 UDP relay.
    Server/UdpRelay.cpp
)
target_include_directories(Server PRIVATE Server)
target_link_libraries(Server PRIVATE mou_shared sqlite3)

add_executable(TestClient
    TestClient/TestClient.cpp
)
target_link_libraries(TestClient PRIVATE mou_shared)

# 친구/메신저 저장소 회귀 테스트 (v7 M2).
#
# 서버를 띄우지 않고 DB 계층만 직접 부른다 - 소켓을 태우면 실패했을 때
# "네트워크 문제인지 로직 문제인지" 를 다시 갈라야 해서 진단이 느려진다.
# 종료 코드 0 = 전부 통과.
add_executable(FriendsTest
    Tests/FriendsTest.cpp
    Server/Friends.cpp
    Server/DirectMessages.cpp
    Server/Accounts.cpp
    Server/Crypto.cpp
)
target_include_directories(FriendsTest PRIVATE Server)
target_link_libraries(FriendsTest PRIVATE mou_shared sqlite3)
