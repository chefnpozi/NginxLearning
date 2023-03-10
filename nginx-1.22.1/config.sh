./configure --prefix=/usr/local/src/nginx   --with-http_stub_status_module --with-http_gzip_static_module \
                                            --with-http_realip_module --with-http_sub_module \
                                            --with-http_ssl_module --with-http_ssl_module \
											--with-pcre-jit \
                                            --with-openssl=${project_name}/TOOL/openssl-1.0.1j \
                                            --with-debug \
                                            --with-cc-opt="-Werror -W -O0" \
                                            --add-module=${project_name}/TOOL/nginx-rtmp-module \
                                            --add-module=${project_name}/TOOL/nginx-module-vts; make && make install
                                            