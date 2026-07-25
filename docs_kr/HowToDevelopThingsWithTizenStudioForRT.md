# RT용 Tizen Studio를 사용하여 개발하는 방법
이 문서에서는 Tizen Studio IDE를 사용하면서 ST Things 프레임워크에서 실행되는 TizenRT 애플리케이션을 개발하는 방법을 설명합니다.
## 전제 조건
TizenRT용 Tizen Studio를 설치하기 전에 다음 필수 구성 요소를 설치해야 합니다. 
- Java 개발 키트(JDK)  
Ubuntu 시스템에 적합한 JDK 버전을 설치하려면 Ubuntu 웹 사이트로 이동하여 Oracle® JDK 버전 8 설치에 대한 자세한 지침을 따르십시오.  
- Ubuntu에서 애플리케이션을 개발하기 위한 패키지 요구 사항  
webkitgtk 패키지를 설치해야 합니다. 터미널 프롬프트에서 다음 명령을 입력합니다.
	``` 
	$ sudo apt-get install libwebkitgtk-1.0-0
	``` 
## 1. RT용 Tizen Studio 설치
RT용 Tizen Studio를 설치하려면:  
1. 소프트웨어 라이센스에 동의합니다.  
2. 설치 디렉터리를 구성합니다.  
3. RT용 Tizen Studio를 설치합니다.  
4. 추가 도구를 설치합니다.  
5. RT용 Tizen Studio를 실행합니다.  

자세한 내용은 [가이드 페이지](https://developer.tizen.org/development/tizen-studio/rt-ide/installing-tizen-studio-rt)에서 확인할 수 있습니다.
 
## 2. 프로젝트 만들기
새로운 SmartThings 프로젝트를 생성하는 방법에는 두 가지가 있습니다. 하나는 로컬 템플릿을 사용하는 것이고, 다른 하나는 원격 Git 저장소를 사용하는 것입니다.
- 로컬 템플릿을 사용하려면:  
템플릿을 선택할 때 **SmartThings 기능** 확인란을 원하는지 확인하세요.  
![템플릿 - 확인란](../docs/media/rt_smartthings_template.png) 
- 원격 Git 저장소를 사용하려면:  
Git 리포지토리에서 프로젝트를 가져올 때 SmartThings 기능** 확인란을 원하는 경우 **Check를 클릭하세요.  
![git - 체크박스](../docs/media/rt_smartthings_git.png)


## 3. 장치 및 리소스 모델 관리
Device/Resource 모델 관리자를 이용하여 SmartThings 서비스와 연동할 기기 모델을 선택할 수 있습니다.  
모델 관리자를 사용하면 다음을 수행할 수 있습니다.  
- [기기, 리소스, 속성 정보 확인](#check-device-resource-and-property-information)  
- [리소스 선택, 추가, 복원](#manage-resources)  
- [스텁 코드 생성](#generate-stub-code)  
- [기기 모델 관리](HowToManageDeviceModel.md)
- [인증서 관리](manage-certificate.md)

SmartThings 기능을 사용하여 새 프로젝트를 생성하는 동안 모델 관리자가 표시됩니다. 나중에 모델 관리자에 다시 액세스하려면 **Project Explorer** 보기에서 프로젝트를 마우스 오른쪽 버튼으로 클릭하고 **ST Things Resource** 관리를 선택하세요.

<a id="check-device-resource-and-property-information"></a>
### **장치, 리소스 및 속성 정보 확인**
정보에 액세스하려면:  
- **Device/Platform** 섹션의 **Device Name** 목록에서 장치 모델을 선택하여 장치, 플랫폼 및 리소스 정보를 확인하세요.  
- **Resource/Property** 섹션의 왼쪽 패널에서 리소스를 선택하면 해당 설명과 속성 정보를 볼 수 있습니다.  
- **Resource/Property** 섹션의 오른쪽 패널에서 속성을 선택하여 해당 설명을 확인하세요.  
![정보 보기](../docs/media/rt_model_select_property.png)  
- SmartThings 웹사이트에서 할당한 MNID 및 Vender ID를 입력하세요. [[여기]](https://smartthings.developer.samsung.com/develop/workspace/ide/create-a-cloud-connected-device.html)를 참조하세요.  
- 클라우드를 통해 기기를 연결하려면 먼저 기기 정보를 등록해야 합니다.  
![MNID 및 공급업체 ID를 입력하세요.](../docs/media/rt_model_input_mnid_n_vid.png)

<a id="manage-resources"></a>
### **리소스 관리**
리소스를 선택, 추가 및 복원할 수 있습니다.  
- 모든 리소스를 선택하려면 **Resource/Property** 도구 모음의 확인란을 클릭하세요.  
![모든 리소스 선택](../docs/media/rt_model_select_all.png)  
- 새 리소스를 추가하려면 다음을 수행하세요.  
   1. **리소스 추가**를 클릭하세요.  
   ![리소스 추가](../docs/media/rt_model_add_resource.png)  
   2. 리소스 추가 창에서 추가할 리소스 유형을 선택하고 해당 정보를 편집한 후 **OK**를 클릭합니다.  
   ![리소스 추가](../docs/media/rt_model_add_resource_window.png)  
   모델 관리자 기본 보기의 **Resource/Property** 섹션의 목록에 새 리소스가 나타납니다.  
- 리소스를 복원하려면 **기본값 복원**를 클릭합니다. 이렇게 하면 장치의 초기 리소스 상태가 복원되고 변경된 모든 내용이 삭제됩니다.

<a id="generate-stub-code"></a>
### **스텁 코드 생성**
모델 관리자에서 **Finish**를 클릭하면 선택한 장치에 대해 리소스 처리를 위한 코드가 자동으로 생성됩니다.  
![Model MAnager가 생성한 소스 코드](../docs/media/rt_model_export_model_window.png)  
프로젝트가 생성되면 선택한 장치 및 리소스에 대한 템플릿 코드가 자동으로 생성되고 편집을 위해 열립니다.  
![소스 코드 샘플](../docs/media/rt_model_code_opened.png)  
생성된 파일 목록은 다음과 같습니다.
- <common_handlers.c>
	- Reset, 소유권 이전 및 상태 변경 핸들러
	- 원하는 기능을 위해서는 추가 코드를 작성해야 합니다.

- <resource_<uri>.c>
	- 리소스에 대한 핸들러 가져오기 및 설정
	- 원하는 기능을 위해서는 추가 코드를 작성해야 합니다.
	- 리소스 파일의 주석 또는 TODO를 참조하세요.

- <Makefile>, <Make.defs>
	- 프로젝트 빌드를 위한 Makefile

- <st_things_main.c>, <things.c>, <things.h>
	- 프로젝트 초기화, 핸들러 등록 및 메인 루프

나중에 프로젝트에서 SmartThings 리소스를 편집하는 경우 모델 관리자는 **.bak** 확장자를 사용하여 기존 소스 파일을 자동으로 백업합니다.  
![백업 파일](../docs/media/rt_model_manage_backups.png)

## 4. 프로젝트 빌드
프로젝트를 플래시하거나 디버깅하기 전에 프로젝트를 빌드해야 합니다.  
다음 두 가지 방법으로 TizenRT 프로젝트를 빌드할 수 있습니다.  
- [배치 빌드 사용](#Use-Batch-Build)
- [프로젝트 빌드 명령](#Use-Build-Project)

<a id="Use-Batch-Build"></a>
### 일괄 빌드 사용
일괄 빌드 명령을 사용하여 프로젝트를 빌드하려면 다음을 수행하세요.  
1. **프로젝트 탐색기** 보기에서 프로젝트를 선택합니다.  
2. 선택한 프로젝트를 빌드하려면 다음 중 하나를 사용합니다.  
	- Tizen Studio for RT 메뉴에서 **프로젝트 > 일괄 빌드 프로젝트**를 선택합니다.  
	!['일괄 빌드 프로젝트'를 선택하세요.](../docs/media/rt_build_smartthings.png)  
	- RT용 Tizen Studio 도구 모음에서 **Build TizenRT Project** 아이콘을 클릭합니다.  
	![아이콘을 클릭하세요](../docs/media/rt_build_smartthings_menu.png)  
3. 빌드 TizenRT 프로젝트 마법사에서 보드를 **artik053**로 선택하고 빌드 옵션을 **st_things**로 선택합니다. 프로젝트를 선택하고 **OK**를 클릭합니다.  
![빌드 옵션](../docs/media/rt_build_option_smartthings.png)  
**Console** 뷰에서 빌드 로그를 확인할 수 있습니다.  
![로그 작성](../docs/media/rt_build_logs_smartthings.png)  

<a id="Use-Build-Project"></a>
### 빌드 프로젝트 사용
프로젝트 빌드 명령을 사용하여 프로젝트를 빌드하려면 다음을 수행하세요.  
1. **프로젝트 탐색기** 보기에서 프로젝트를 선택합니다.  
2. RT용 Tizen Studio 도구 모음에서 **빌드 TizenRT 프로젝트 아이콘 옆에 있는 화살표를 클릭하고 **를 선택하고 **보드 선택**를 선택합니다.  
![보드 선택](../docs/media/rt_build_dropdown_menu.png)  
3. Select Board and PreDefine Option 창에서 보드를 **artik053**로 선택하고 빌드 옵션을 **st_things**로 선택하고 프로젝트 빌드를 클릭합니다. **OK**.  
![빌드 옵션](../docs/media/rt_build_option_smartthings.png)  
4. Tizen Studio for RT 도구 모음에서 **Project > Build Project**를 선택하여 프로젝트를 빌드합니다.  
!['프로젝트 빌드'를 선택하세요.](../docs/media/rt_build_build_project.png)  
**Console** 뷰에서 빌드 로그를 확인할 수 있습니다.  
![로그 작성](../docs/media/rt_build_logs_smartthings.png)  

## 5. 프로젝트 플래시  
프로젝트를 보드에 업로드하려면 프로젝트를 플래시해야 합니다.  
1. 보드를 컴퓨터에 연결합니다.  
2. **프로젝트 탐색기** 보기에서 프로젝트를 선택합니다.  
3. TizenRT 프로젝트를 플래시하려면:  
     나. RT용 Tizen Studio 도구 모음에서 **Flash** 아이콘을 클릭합니다.  
    ii. 플래시 옵션 **'ALL'**를 선택하고 **OK**를 클릭합니다.  
![플래시 옵션](../docs/media/rt_flash_option.png)  
**Console** 보기에서 상태를 볼 수 있습니다.
![플래시 로그](../docs/media/rt_flash_logs.png)  

