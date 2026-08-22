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
)
target_include_directories(Server PRIVATE Server)
target_link_libraries(Server PRIVATE mou_shared sqlite3)

add_executable(TestClient
    TestClient/TestClient.cpp
)
target_link_libraries(TestClient PRIVATE mou_shared)
