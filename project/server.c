#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        perror("Failed to receive data");
        close(client_socket);
        return;
    }

    buffer[received] = '\0';

    // HTML 페이지 제공
    if (strstr(buffer, "GET /upload.html") != NULL || strstr(buffer, "GET /") != NULL) {
        FILE *html_file = fopen("upload.html", "r");
        if (!html_file) {
            perror("Failed to open HTML file");
            close(client_socket);
            return;
        }

        char html_buffer[BUFFER_SIZE];
        size_t bytes_read = fread(html_buffer, 1, sizeof(html_buffer), html_file);

        const char *header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n";
        send(client_socket, header, strlen(header), 0);
        send(client_socket, html_buffer, bytes_read, 0);

        fclose(html_file);
        close(client_socket);
        return;
    }

    // 파일 업로드 처리
    if (strstr(buffer, "POST /upload") != NULL) {
        printf("POST /upload request received\n");

        FILE *file = fopen("uploaded_file", "wb");
        if (!file) {
            perror("File open error");
            close(client_socket);
            return;
        }

        // HTTP 요청 본문에서 파일 데이터 추출
        char *data = strstr(buffer, "\r\n\r\n") + 4; // 헤더 끝 이후 데이터 시작
        size_t initial_data_size = received - (data - buffer);

        // 초기 데이터 쓰기
        size_t written = fwrite(data, 1, initial_data_size, file);
        printf("Initial bytes written: %zu\n", written);

        // 추가 데이터 받기
        while ((received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
            size_t additional_written = fwrite(buffer, 1, received, file);
            printf("Additional bytes written: %zu\n", additional_written);
        }

        fclose(file);
        printf("File uploaded successfully\n");

        // 업로드된 파일 내용 디버깅
        FILE *uploaded_file = fopen("uploaded_file", "rb");
        if (uploaded_file) {
            char file_content[BUFFER_SIZE];
            size_t read_size = fread(file_content, 1, sizeof(file_content) - 1, uploaded_file);
            file_content[read_size] = '\0'; // NULL로 종료
            printf("Uploaded file content:\n%s\n", file_content);
            fclose(uploaded_file);
        }

        // HTTP 응답 전송
        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n\r\n"
            "File uploaded successfully";
        send(client_socket, response, strlen(response), 0);
        close(client_socket);
        return;
    }

    close(client_socket);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // 주소 설정
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 소켓 바인딩
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(1);
    }

    // 클라이언트 연결 대기
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(1);
    }

    printf("Server started on port %d...\n", PORT);

    // 클라이언트 요청 처리
    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len)) >= 0) {
        handle_client(client_socket);
    }

    close(server_socket);
    return 0;
}


