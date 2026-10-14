function help {
    cat << EOF
Usage:
    ./build_and_test.sh tests_constant_time_compare

    ./build_and_test.sh build_http_server
    ./build_and_test.sh build_https_server
    ./build_and_test.sh build_for_ubuntu
    ./build_and_test.sh build_rodata

    ./build_and_test.sh clean
    ./build_and_test.sh help

EOF
}

function build_https_server {
    gcc -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fstrict-flex-arrays=3 \
		-fno-strict-aliasing -fno-strict-overflow -fno-delete-null-pointer-checks \
		-fcf-protection=full -Wall -Wextra -Wformat -Wformat-security -fPIE -pie \
		-Wl,-z,relro,-z,now -Wl,-z,noexecstack -o server html_data.c server.c -lssl -lcrypto
}

function build_http_server {
	gcc -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fstrict-flex-arrays=3 -fno-strict-aliasing -fno-strict-overflow \
		-fno-delete-null-pointer-checks -fcf-protection=full \
		-Wall -Wextra -Wformat -Wformat-security -fPIE -pie \
		-Wl,-z,relro,-z,now -Wl,-z,noexecstack -o server html_data.c server.c
}

function build_rodata_block {
    echo "Zusammengeführte Frontends zu einem Byte-Read-Only-Segment-Blockpuffer"
    gcc -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fstrict-flex-arrays=3 -fno-strict-aliasing -fno-strict-overflow \
        -fno-delete-null-pointer-checks -fcf-protection=full -Wall -Wextra -Wformat -Wformat-security -fPIE -pie \
        -Wl,-z,relro,-z,now -Wl,-z,noexecstack -o generate_c_frontend_source generate_c_frontend_source.c
}

function build_tests_constant_time_compare {
    gcc -D_USE_TEST_ENDPOINTS -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fstrict-flex-arrays=3 \
        -fno-strict-aliasing -fno-strict-overflow -fno-delete-null-pointer-checks \
        -fcf-protection=full -Wall -Wextra -Wformat -Wformat-security \
        -fPIE -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -o test_constant_time test_constant_time.c
}

function build_rodata {
    build_rodata_block
    ./generate_c_frontend_source
}

function clean {
    rm server generate_c_frontend_source html_data.c html_data.h test_constant_time test_ixxc
}

function tests_constant_time_compare {
    build_tests_constant_time_compare
    for ((i=0; i<=9; i++)); do
        echo "Test Nr: $i"
        ./test_constant_time
        sleep 5
        echo ""
    done
}

function build_for_ubuntu {
    podman build -t server-builder .
    podman run --rm -v "$(pwd)":/app -v "$(pwd)/../frontends":/frontends server-builder
}

function dispatch {
    local cmd="${1:-}"
    case "$cmd" in
        tests_constant_time_compare)
            tests_constant_time_compare
            ;;
        build_http_server)
            build_http_server
            ;;
        build_https_server)
            build_https_server
            ;;
        clean)
            clean
            ;;
        build_for_ubuntu)
            build_for_ubuntu
            ;;
        build_rodata)
            build_rodata
            ;;
        help|--help|-h)
            help
            ;;
        "")
            help
            ;;
        *)
            echo "Unknown command: $cmd" >&2
            echo >&2
            help >&2
            exit 1
            ;;
    esac
}

dispatch "${1:-}"
