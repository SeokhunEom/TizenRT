# Kconfig-프런트엔드 설치

## 전제 조건
*bison*(또는 지원되는 경우 *byacc*), *flex*, *gperf*, *libncurses5-dev*, *zlib1g-dev*, *gettext* 및 *g++* 패키지를 설치해야 합니다.
```bash
sudo apt-get install bison byacc flex gperf libncurses5-dev zlib1g-dev gettext g++
```

## Kconfig-프런트엔드
1. *kconfig-frontends* 패키지를 다운로드하고 압축을 풉니다.  
사이트 중 하나는 [얀 모린의 프로젝트](http://ymorin.is-a-geek.org/projects/kconfig-frontends)입니다.
	```bash
	tar -xvf kconfig-frontends-x.xx.x.x.tar.bz2
	```

2. *kconfig-frontends* 폴더로 이동합니다.
	```bash
	cd kconfig-frontends-x.xx.x.x
	```

3. 구성 및 빌드.
	```bash
	./configure --enable-mconf --disable-gconf --disable-qconf
	make
	sudo make install
	```

## 트러블슈팅
[문제 해결](https://github.com/Samsung/TizenRT/blob/master/docs/TroubleShooting.md)를 참조하세요.
