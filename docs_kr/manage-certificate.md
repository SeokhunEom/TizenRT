# SmartThings&trade; 인증서 관리
SmartThings&trade;는 보안을 위해 OpenSSL을 사용합니다. 신뢰할 수 있는 개발자 및 제조업체만 SmartThings 클라우드와 통신하는 장치를 개발할 수 있도록 하려면 SmartThings&trade; 서비스를 사용하려면 장치에 대한 인증서를 받아야 합니다.

## 목차
- [인증서 관리자 사용](#use-a-certificate-manager) 
- [인증서 만들기](#create-a-certificate)  
- [인증서 가져오기](#import-a-certificate)  
- [인증서 활성화](#activate-a-certificate)  

<a id="use-a-certificate-manager"></a>
## 인증서 관리자 사용
Tizen RT IDE에서는 **SmartThings 인증서 관리자**를 사용하여 생성, 제거, 가져오기, 활성화 등 다양한 인증서 관련 작업을 수행할 수 있습니다.

![SmartThings 인증서 관리자](../docs/media/rt_cert_manager.png)

프로젝트를 플래시하려고 할 때 활성으로 설정된 인증서가 없으면 인증서 관리자가 자동으로 시작됩니다. 인증서 관리자를 수동으로 시작하려면 Tizen Studio for RT 메뉴에서 **Tools &gt; ST Certificate Manager**를 선택합니다.

<a name="create"></a>
<a id="create-a-certificate"></a>
## 인증서 생성

새 인증서를 생성하려면 CSR(인증서 서명 요청)을 생성하여 [개발자 작업공간](https://devworkspace.developer.samsung.com/smartthingsconsole/iotweb/site/index.html#/main) 웹 사이트에 제출해야 합니다.

SmartThings&trade; 서비스를 사용하기 위해 새 인증서를 생성하려면:

1.  SmartThings 인증서 관리자에서 **New**를 클릭합니다.
2. 인증서 생성 대화 상자에서 인증서 생성에 필요한 정보를 입력하고 **OK**를 클릭합니다.

    -   **인증서 이름**: 인증서의 고유 이름
    -   **Country**: 조직이 위치한 국가를 나타내는 2자리 ISO 코드
    -   **State/Province**: 조직이 위치한 주 또는 도
    -   **Locality**: 조직이 위치한 도시
    -   **Organization**: 조직의 법적 이름
    -   **조직 단위**: 인증서를 처리하는 조직의 부서
    -   **일반 이름**: 서버의 정규화된 도메인 이름 또는 사용자 이름
    -   **Email**: 조직에 연락하는 데 사용되는 이메일 주소

    ![인증서 생성 대화 상자에 데이터 입력](../docs/media/rt_cert_create.png)

3. 인증서 파일을 다운로드하고 인증서 관리자에 입력합니다.

    -  [개발자 작업공간](https://devworkspace.developer.samsung.com/smartthingsconsole/iotweb/site/index.html#/main) 웹 사이트에 로그인합니다.
    -  웹 브라우저에서 **공용 도구 &gt; 인증서 서명 요청 &gt; 장치**를 선택합니다.
    -  인증서 생성 대화 상자에서 **CSR(인증서 서명 요청)** 필드의 내용을 클립보드에 복사합니다.
    -  웹 브라우저에서 복사한 내용을 **CSR** 필드에 붙여넣고 **Request**를 클릭합니다. 인증서는 **Certificate** 필드에서 생성됩니다.
    -  **Download**를 클릭하고 인증서 파일이 저장되는 위치를 지정합니다.
    -  인증서 생성 대화 상자에서 **찾아보기**를 클릭하고 다운로드한 인증서 파일을 선택한 다음 **OK**를 클릭합니다.

    ![인증서 생성 대화 상자에서 인증서 만들기](../docs/media/rt_cert_create_download.png)

<a name="import"></a>
<a id="import-a-certificate"></a>
## 인증서 가져오기

기존 인증서를 가져오려면:
1. SmartThings 인증서 관리자에서 **가져오기**를 클릭합니다.
2. 인증서 가져오기 대화 상자에서 인증서를 가져오는 데 필요한 정보를 입력하고 **OK**를 클릭합니다.

    -   **인증서 이름**: 인증서의 고유 이름
    -   **개인 키**: `.der` 형식의 개인 키 파일
    -   **Certificate**: `.pem` 형식의 인증서 파일

    ![인증서 가져오기 대화 상자](../docs/media/rt_cert_import.png)

<a name="activate"></a>
<a id="activate-a-certificate"></a>
## 인증서 활성화

프로젝트를 플래시하려면 활성 인증서가 있어야 합니다. Tizen RT IDE는 플래싱 프로세스 중에 활성화된 인증서를 프로젝트에 복사합니다.

인증서를 활성화하려면:

1.  SmartThings 인증서 관리자에서 활성화하려는 인증서를 선택합니다.
2.  **활성화 설정**를 클릭합니다.
