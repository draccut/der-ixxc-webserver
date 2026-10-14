FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

CMD ["gcc", \
     "-O2", \
     "-fstack-protector-strong", \
     "-D_FORTIFY_SOURCE=3", \
     "-fstrict-flex-arrays=3", \
     "-fno-strict-aliasing", \
     "-fno-strict-overflow", \
     "-fno-delete-null-pointer-checks", \
     "-fcf-protection=full", \
     "-Wall", "-Wextra", \
     "-Wformat", "-Wformat-security", \
     "-fPIE", "-pie", \
     "-Wl,-z,relro,-z,now", \
     "-Wl,-z,noexecstack", \
     "-o", "server", \
     "html_data.c", "server.c"]
