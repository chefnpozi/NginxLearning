./configure --prefix=/usr/local/src/nginx   --with-http_stub_status_module --with-http_gzip_static_module \
                                            --with-http_realip_module --with-http_sub_module \
                                            --with-http_ssl_module --with-http_ssl_module \
											--with-pcre-jit \
                                            --with-openssl=/home/chezi/local/our_code/TOOL/openssl-1.0.1j \
                                            --with-debug \
                                            --with-cc-opt="-Werror -W -O0" \
                                            --add-module=/home/chezi/local/our_code/TOOL/nginx-rtmp-module; make && make install