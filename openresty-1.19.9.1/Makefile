.PHONY: all install clean

all:
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/LuaJIT-2.1-20210510 && $(MAKE) TARGET_STRIP=@: CCDEBUG=-g Q= XCFLAGS='-DLUAJIT_ENABLE_LUA52COMPAT -DLUA_USE_APICHECK -DLUA_USE_ASSERT' CC=cc PREFIX=/usr/local/src/openresty/luajit
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-cjson-2.1.0.8 && $(MAKE) DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_CMODULE_DIR=/usr/local/src/openresty/lualib LUA_MODULE_DIR=/usr/local/src/openresty/lualib CJSON_CFLAGS="-g -O -fpic" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-signal-0.03 && $(MAKE) DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_LIB_DIR=/usr/local/src/openresty/lualib CFLAGS="-g -O -Wall -fpic" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-redis-parser-0.13 && $(MAKE) DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_LIB_DIR=/usr/local/src/openresty/lualib CFLAGS="-g -O -Wall" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-rds-parser-0.06 && $(MAKE) DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_LIB_DIR=/usr/local/src/openresty/lualib CFLAGS="-g -O -Wall" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/nginx-1.19.9 && $(MAKE)

install: all
	mkdir -p $(DESTDIR)/usr/local/src/openresty/
	-cp /home/chezi/resty_code/code/openresty-1.19.9.1/COPYRIGHT $(DESTDIR)/usr/local/src/openresty/
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/LuaJIT-2.1-20210510 && $(MAKE) install TARGET_STRIP=@: CCDEBUG=-g Q= XCFLAGS='-DLUAJIT_ENABLE_LUA52COMPAT -DLUA_USE_APICHECK -DLUA_USE_ASSERT' CC=cc PREFIX=/usr/local/src/openresty/luajit DESTDIR=$(DESTDIR)
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-cjson-2.1.0.8 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_CMODULE_DIR=/usr/local/src/openresty/lualib LUA_MODULE_DIR=/usr/local/src/openresty/lualib CJSON_CFLAGS="-g -O -fpic" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-signal-0.03 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_LIB_DIR=/usr/local/src/openresty/lualib CFLAGS="-g -O -Wall -fpic" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-redis-parser-0.13 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_LIB_DIR=/usr/local/src/openresty/lualib CFLAGS="-g -O -Wall" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-rds-parser-0.06 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_INCLUDE_DIR=/home/chezi/resty_code/code/openresty-1.19.9.1/build/luajit-root/usr/local/src/openresty/luajit/include/luajit-2.1 LUA_LIB_DIR=/usr/local/src/openresty/lualib CFLAGS="-g -O -Wall" CC=cc
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-dns-0.22 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-memcached-0.16 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-redis-0.29 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-mysql-0.24 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-string-0.14 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-upload-0.10 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-websocket-0.08 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-lock-0.08 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-lrucache-0.11 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-core-0.1.22 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-upstream-healthcheck-0.06 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-limit-traffic-0.07 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-resty-shell-0.03 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/lua-tablepool-0.02 && $(MAKE) install DESTDIR=$(DESTDIR) LUA_LIB_DIR=/usr/local/src/openresty/lualib INSTALL=/home/chezi/resty_code/code/openresty-1.19.9.1/build/install
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/opm-0.0.6 && /home/chezi/resty_code/code/openresty-1.19.9.1/build/install bin/* $(DESTDIR)/usr/local/src/openresty/bin/
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/resty-cli-0.28 && /home/chezi/resty_code/code/openresty-1.19.9.1/build/install bin/* $(DESTDIR)/usr/local/src/openresty/bin/
	cp /home/chezi/resty_code/code/openresty-1.19.9.1/build/resty.index $(DESTDIR)/usr/local/src/openresty/
	cp -r /home/chezi/resty_code/code/openresty-1.19.9.1/build/pod $(DESTDIR)/usr/local/src/openresty/
	cd /home/chezi/resty_code/code/openresty-1.19.9.1/build/nginx-1.19.9 && $(MAKE) install DESTDIR=$(DESTDIR)
	mkdir -p $(DESTDIR)/usr/local/src/openresty/site/lualib $(DESTDIR)/usr/local/src/openresty/site/pod $(DESTDIR)/usr/local/src/openresty/site/manifest
	ln -sf /usr/local/src/openresty/nginx/sbin/nginx $(DESTDIR)/usr/local/src/openresty/bin/openresty

clean:
	rm -rf build *.exe *.dll openresty-*
