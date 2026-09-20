cmake_minimum_required(VERSION 3.19)
project(pi_Desktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Concurrent Gui Qml Quick QuickControls2)

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Windows 使用 GUI 子系统，双击程序或右键启动时不创建附带控制台。
qt_add_executable(pi_Desktop
    WIN32
    MACOSX_BUNDLE
    main.cpp
    src/app/AppController.h
    src/app/AppController.cpp
    src/agent/AgentSessionController.h
    src/agent/AgentSessionController.cpp
    src/chat/ChatModel.h
    src/chat/ChatModel.cpp
    src/config/AppSettings.h
    src/config/AppSettings.cpp
    src/pi/PiEvent.h
    src/pi/PiEvent.cpp
    src/pi/PiProcess.h
    src/pi/PiProcess.cpp
    src/pi/PiRpcClient.h
    src/pi/PiRpcClient.cpp
    src/session/SessionModel.h
    src/session/SessionModel.cpp
    src/session/ProjectModel.h
    src/session/ProjectModel.cpp
)

target_include_directories(pi_Desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# PNG 随 Qt 资源打包，窗口及页头共用标识；ICO 嵌入 Windows 可执行文件。
qt_add_resources(pi_Desktop app_icon
    PREFIX "/"
    FILES assets/pi-desktop.png)
if(WIN32)
    enable_language(RC)
    configure_file(assets/pi-desktop.rc.in "${CMAKE_CURRENT_BINARY_DIR}/pi-desktop.rc" @ONLY)
    target_sources(pi_Desktop PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pi-desktop.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/pi-desktop.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/pi-desktop.ico")
endif()

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/pages/ChatPage.qml PROPERTIES QT_RESOURCE_ALIAS pages/ChatPage.qml)
foreach(component
        ChatView UserMessage AssistantMessage SystemMessage ToolCall PromptEditor SessionSidebar ContextPanel WorkspaceScrollBar WorkingIndicator RemovablePathComboBox)
    set_source_files_properties(qml/components/${component}.qml
        PROPERTIES QT_RESOURCE_ALIAS components/${component}.qml)
endforeach()

qt_add_qml_module(pi_Desktop
    URI PiDesktop
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/ChatPage.qml
        qml/components/ChatView.qml
        qml/components/WorkspaceScrollBar.qml
        qml/components/WorkingIndicator.qml
        qml/components/RemovablePathComboBox.qml
        qml/components/UserMessage.qml
        qml/components/AssistantMessage.qml
        qml/components/SystemMessage.qml
        qml/components/ToolCall.qml
        qml/components/PromptEditor.qml
        qml/components/SessionSidebar.qml
        qml/components/ContextPanel.qml
)

target_link_libraries(pi_Desktop
    PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

include(GNUInstallDirs)
install(TARGETS pi_Desktop
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET pi_Desktop
    OUTPUT_SCRIPT deploy_script
    # 发布包不附带 Qt 多语言翻译资源。
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# 菜单安装脚本与程序同目录，注册发布路径而不是开发构建路径。
if(WIN32)
    install(FILES scripts/install-folder-menu.cmd scripts/uninstall-folder-menu.cmd
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
    # 参数校验必须在启动 Agent 或修改注册表之前失败。
    add_test(NAME folder_menu_conflicting_options
        COMMAND pi_Desktop --install-folder-menu --uninstall-folder-menu -platform offscreen)
    add_test(NAME folder_menu_empty_workspace
        COMMAND pi_Desktop --workspace "" -platform offscreen)
    add_test(NAME folder_menu_file_workspace
        COMMAND pi_Desktop --workspace "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" -platform offscreen)
    set_tests_properties(folder_menu_conflicting_options folder_menu_empty_workspace
        folder_menu_file_workspace PROPERTIES WILL_FAIL TRUE)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(folder_menu_conflicting_options folder_menu_empty_workspace
            folder_menu_file_workspace PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(pi_desktop_tests
        tests/ChatModelTest.cpp
        src/config/AppSettings.h
        src/config/AppSettings.cpp
        src/chat/ChatModel.h
        src/chat/ChatModel.cpp
        src/session/SessionModel.h
        src/session/SessionModel.cpp
    )
    target_include_directories(pi_desktop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pi_desktop_tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Test)
    add_test(NAME chat_model COMMAND pi_desktop_tests)

    # 项目配置和 cwd 归组测试使用临时目录，不读取或修改用户项目配置。
    qt_add_executable(pi_project_tests
        tests/ProjectModelTest.cpp
        src/session/SessionModel.h src/session/SessionModel.cpp
        src/session/ProjectModel.h src/session/ProjectModel.cpp)
    target_include_directories(pi_project_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pi_project_tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Test)
    add_test(NAME project_model COMMAND pi_project_tests)

    # 使用自包含 RPC 桩测试控制器，无需真实 Pi 或模型服务。
    qt_add_executable(pi_agent_tests
        tests/AgentSessionTest.cpp
        src/app/AppController.h src/app/AppController.cpp
        src/config/AppSettings.h src/config/AppSettings.cpp
        src/session/SessionModel.h src/session/SessionModel.cpp
        src/session/ProjectModel.h src/session/ProjectModel.cpp
        src/agent/AgentSessionController.h src/agent/AgentSessionController.cpp
        src/chat/ChatModel.h src/chat/ChatModel.cpp
        src/pi/PiEvent.h src/pi/PiEvent.cpp
        src/pi/PiProcess.h src/pi/PiProcess.cpp
        src/pi/PiRpcClient.h src/pi/PiRpcClient.cpp)
    target_include_directories(pi_agent_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pi_agent_tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Test)
    add_test(NAME agent_session COMMAND pi_agent_tests)

    # 使用 Qt 自带运行器验证滑块状态和实际鼠标拖动，无需启动 Agent。
    find_program(QMLTESTRUNNER_EXECUTABLE NAMES qmltestrunner
        HINTS "${Qt6_DIR}/../../../bin")
    if(QMLTESTRUNNER_EXECUTABLE)
        add_test(NAME workspace_scrollbar
            COMMAND "${QMLTESTRUNNER_EXECUTABLE}"
                -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml"
                -platform offscreen
                -o "${CMAKE_CURRENT_BINARY_DIR}/workspace-scrollbar-tests.txt,txt")
    endif()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(chat_model agent_session project_model PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
