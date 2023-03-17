./configure --prefix=/usr/local/src/openresty       --with-http_stub_status_module --with-http_gzip_static_module \
                                                    --with-http_realip_module --with-http_sub_module \
                                                    --with-http_ssl_module --with-http_ssl_module \
                                                    --with-pcre-jit \
                                                    --with-luajit \
                                                    --with-debug \
                                                    --with-stream \
                                                    --with-openssl=${resty_path}/TOOL/openssl-1.1.1t \
                                                    --with-cc-opt="-Werror -W -O0" \
                                                    --add-module=${resty_path}/TOOL/nginx-module-vts \
                                                    --add-module=${resty_path}/TOOL/nginx-module-sts \
                                                    --add-module=${resty_path}/TOOL/nginx-module-stream-sts \
                                                    --add-module=${resty_path}/TOOL/nginx-rtmp-module ; make && make install

# --add-module=${resty_path}/TOOL/nginx-rtmp-module \
# --add-module=${resty_path}/TOOL/nginx-module-sts \
# --add-module=${resty_path}/TOOL/nginx-module-stream-sts \
# --add-module=${resty_path}/TOOL/nginx-http-flv-module \ 
                                            