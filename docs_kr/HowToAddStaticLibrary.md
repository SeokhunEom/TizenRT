# 정적 라이브러리 추가 방법

정적 라이브러리를 포함하는 방법에는 [아치 라이브러리에 추가](#adding-it-into-arch-library)와 [새 라이브러리로 추가](#adding-it-as-a-new-library)의 두 가지 방법이 있습니다.

## 아치 라이브러리에 추가

Arch의 Makefile 또는 Make.defs에는 아래 언급된 특정 아키텍처에 대한 정적 라이브러리가 포함될 수 있습니다. 관례적으로 지정된 정적 라이브러리는
는 최종 빌드에서 *libarch.a*에 추가됩니다.
```
VPATH += <LIB_PATH>
EXTRA_LIBS += <LIB_PATH>/<LIB_NAME>.a
```
*LIB_PATH*는 *os/arch/arm/src*의 상대 경로여야 합니다.

예를 들어,
```
VPATH += chip/abc
EXTRA_LIBS += chip/abc/libnew.a
```

이를 통해 *os/arch/arm/src* 폴더에 있는 Makefile 내에서 정적 라이브러리를 추가할 수 있습니다. 지정된 라이브러리 *libnew.a*가 *libarch.a* 아래에 병합됩니다.
```
$(OUTBIN_DIR)/tinyara$(EXEEXT): $(HEAD_OBJ) board/libboard$(LIBEXT)
	$(Q) echo "LD: tinyara"
	$(Q) $(LD) --entry=__start $(LDFLAGS) $(LIBPATHS) $(EXTRA_LIBPATHS) \
		-o $(TINYARA) $(HEAD_OBJ) $(EXTRA_OBJS) \
		--start-group $(LDLIBS) $(EXTRA_LIBS) $(LIBGCC) --end-group -Map $(TOPDIR)/../build/output/bin/tinyara.map
```

## 새 라이브러리로 추가

TizenRT를 사용하면 정적 라이브러리를 기존 라이브러리에 병합하지 않고도 별도의 엔터티로 포함할 수 있습니다. 다음 단계에서는 정적 라이브러리를 포함하는 방법을 설명합니다.

1. *Libtargets.mk*에 정적 라이브러리를 추가합니다.
    ```
    $(LIBRARIES_DIR)$(DELIM)<LIB_NAME>$(LIBEXT): <LIB_PATH>$(DELIM)<LIB_NAME>$(LIBEXT)
    	$(Q) install <LIB_PATH>$(DELIM)<LIB_NAME>$(LIBEXT) $(LIBRARIES_DIR)$(DELIM)<LIB_NAME>$(LIBEXT)
    ```

2. *FlatLibs.mk*, *ProtectedLibs.mk* 및 *KernelLibs.mk*에 정적 라이브러리를 추가합니다.

    커널 라이브러리의 경우,
    ```
    TINYARALIBS += $(LIBRARIES_DIR)$(DELIM)<LIB_NAME>$(LIBEXT)
    ```

    사용자 라이브러리의 경우,
    ```
    USERLIBS += $(LIBRARIES_DIR)$(DELIM)<LIB_NAME>$(LIBEXT)
    ```

    플랫 빌드에서는 TINYARALIBS와 USERLIBS 사이에 차이가 없습니다.  
    그러나 보호된 빌드 및 커널 빌드에서는 TizenRT가 커널 공간과 사용자 공간을 분할합니다. 따라서 적절한 공간에 새로운 정적 라이브러리가 포함되어야 합니다.

*LIB_PATH*는 *os*의 상대 경로여야 합니다.
