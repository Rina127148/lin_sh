FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    make \
    libpthread-stubs0-dev \
    && rm -rf /var/lib/apt/lists/*

# Создаем домашнюю директорию и необходимые файлы
RUN mkdir -p /root/users && \
    touch /root/.kubsh_history && \
    chmod 644 /root/.kubsh_history

COPY . /app
WORKDIR /app

RUN make kubsh
RUN cp kubsh /usr/local/bin/

CMD ["kubsh"]
