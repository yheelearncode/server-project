#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <qrencode.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_LINE 1024
#define BUFFER_SIZE 8192
#define HTML_FILENAME "upload.html"
#define UPLOAD_DIR "uploads/"

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // 파일 처리에 대한 Mutex

// UPLOAD_DIR 생성 함수
void ensure_upload_dir() {
    struct stat st = {0};

    if (stat(UPLOAD_DIR, &st) == -1) { // 디렉터리가 존재하지 않으면
        if (mkdir(UPLOAD_DIR, 0700) == 0) { // 디렉터리 생성
            printf("Directory '%s' created successfully.\n", UPLOAD_DIR);
        } else { // 디렉터리 생성 실패
            perror("Failed to create upload directory");
            exit(EXIT_FAILURE);
        }
    }
}

void replace_action_url(const char *ip, const char *port) {
    FILE *file = fopen(HTML_FILENAME, "r");
    if (!file) {
        perror("파일 열기 실패");
        exit(EXIT_FAILURE);
    }

    FILE *temp = fopen("temp.html", "w");
    if (!temp) {
        perror("임시 파일 생성 실패");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    char search[] = "<form action=\"http://";
    int replaced = 0;

    while (fgets(line, sizeof(line), file)) {
        char *pos = strstr(line, search);
        if (pos && !replaced) {
            fprintf(temp, "<form action=\"http://%s:%s/upload\" method=\"POST\" enctype=\"multipart/form-data\">\n", ip, port);
            replaced = 1;
        } else {
            fputs(line, temp);
        }
    }

    fclose(file);
    fclose(temp);

    if (remove(HTML_FILENAME) != 0) {
        perror("원본 파일 삭제 실패");
        exit(EXIT_FAILURE);
    }
    if (rename("temp.html", HTML_FILENAME) != 0) {
        perror("임시 파일 이름 변경 실패");
        exit(EXIT_FAILURE);
    }

    printf("HTML 파일이 성공적으로 업데이트되었습니다.\n");
}

void print_qr_code(const char *url) {
    QRcode *qrcode = QRcode_encodeString(url, 0, QR_ECLEVEL_L, QR_MODE_8, 1);
    if (!qrcode) {
        fprintf(stderr, "QR 코드 생성 실패\n");
        exit(EXIT_FAILURE);
    }

    for (int y = 0; y < qrcode->width + 2; y++) {
        printf("██");
    }
    printf("\n");

    for (int y = 0; y < qrcode->width; y++) {
        printf("██");
        for (int x = 0; x < qrcode->width; x++) {
            if (qrcode->data[y * qrcode->width + x] & 0x01) {
                printf("██");
            } else {
                printf("  ");
            }
        }
        printf("██\n");
    }

    for (int y = 0; y < qrcode->width + 2; y++) {
        printf("██");
    }
    printf("\n");

    QRcode_free(qrcode);
}

// 파일에서 메타데이터와 boundary 제거
void remove_metadata_from_file(const char *input_filename, const char *output_filename) {
    FILE *input_file = fopen(input_filename, "r");
    if (!input_file) {
        perror("Failed to open input file");
        return;
    }

    FILE *output_file = fopen(output_filename, "w");
    if (!output_file) {
        perror("Failed to create output file");
        fclose(input_file);
        return;
    }

    char line[MAX_LINE];
    int in_metadata = 1;  // Assume metadata starts at the top
    const char *boundary_start = "------WebKitFormBoundary";  // Boundary 시작 부분
    size_t boundary_length = strlen(boundary_start);

    while (fgets(line, sizeof(line), input_file)) {
        // Skip lines that are part of the boundary
        if (strncmp(line, boundary_start, boundary_length) == 0) {
            // If it's the boundary line, skip it
            continue;
        }

        // Skip the ending boundary line (e.g., "------WebKitFormBoundary--")
        if (strstr(line, "--") != NULL) {
            continue;
        }

        // When the metadata ends (empty line \r\n\r\n), start saving the file data
        if (in_metadata && strcmp(line, "\r\n") == 0) {
            in_metadata = 0;  // Start writing actual data
            continue;
        }

        // Once metadata is removed, write the actual data
        if (!in_metadata) {
            fputs(line, output_file);
        }
    }

    fclose(input_file);
    fclose(output_file);

    printf("Metadata removed successfully. Data saved to '%s'.\n", output_filename);

    // 메타데이터 제거 후 기존 파일 삭제
    if (remove(input_filename) != 0) {
        perror("Failed to delete original file");
    } else {
        printf("Original file '%s' deleted successfully.\n", input_filename);
    }
}

// 파일 확장자 확인 함수
int is_pdf(const char *filename) {
    // 확장자가 .pdf이면 1을 반환
    const char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".pdf") == 0) {
        return 1;
    }
    return 0;
}

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        perror("Failed to receive data");
        close(client_socket);
        pthread_exit(NULL);
    }

    buffer[received] = '\0';

    if (strstr(buffer, "GET /upload.html") != NULL || strstr(buffer, "GET /") != NULL) {
        FILE *html_file = fopen("upload.html", "r");
        if (!html_file) {
            perror("Failed to open HTML file");
            close(client_socket);
            pthread_exit(NULL);
        }

        fseek(html_file, 0, SEEK_END);
        long html_size = ftell(html_file);
        fseek(html_file, 0, SEEK_SET);

        char *html_buffer = malloc(html_size + 1);
        fread(html_buffer, 1, html_size, html_file);
        html_buffer[html_size] = '\0';

        const char *header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n";
        send(client_socket, header, strlen(header), 0);
        send(client_socket, html_buffer, html_size, 0);

        free(html_buffer);
        fclose(html_file);
        close(client_socket);
        pthread_exit(NULL);
    }

    if (strstr(buffer, "POST /upload") != NULL) {
        printf("POST /upload request received\n");

        char *filename_start = strstr(buffer, "filename=\"");
        char filename[256] = {0};
        if (filename_start) {
            filename_start += 10;
            char *filename_end = strchr(filename_start, '"');
            if (filename_end) {
                strncpy(filename, filename_start, filename_end - filename_start);
            }
        }

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s%s", UPLOAD_DIR, filename);

        pthread_mutex_lock(&file_mutex);

        FILE *file = fopen(filepath, "wb");
        if (!file) {
            perror("File open error");
            pthread_mutex_unlock(&file_mutex);
            close(client_socket);
            pthread_exit(NULL);
        }

        char *content_length_start = strstr(buffer, "Content-Length: ");
        int content_length = 0;
        if (content_length_start) {
            content_length_start += 16;
            content_length = atoi(content_length_start);
        }

        char *data_start = strstr(buffer, "\r\n\r\n") + 4;
        size_t initial_data_size = received - (data_start - buffer);
        fwrite(data_start, 1, initial_data_size, file);

        int total_received = initial_data_size;

        while (total_received < content_length) {
            received = recv(client_socket, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            fwrite(buffer, 1, received, file);
            total_received += received;
        }

        fclose(file);
        pthread_mutex_unlock(&file_mutex);

        printf("File '%s' uploaded successfully. Total bytes: %d\n", filename, total_received);

        const char *redirect_response =
            "HTTP/1.1 303 See Other\r\n"
            "Location: /upload.html\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n";
        send(client_socket, redirect_response, strlen(redirect_response), 0);

        // PDF 파일은 메타데이터를 제거하지 않고, 텍스트나 HTML 파일만 메타데이터를 제거
        if (is_pdf(filename)) {
            printf("PDF file detected, skipping metadata removal.\n");
        } else {
            // Remove metadata from the uploaded file and save as a new file
            char cleaned_filepath[1024];
            snprintf(cleaned_filepath, sizeof(cleaned_filepath), "%s_cleaned", filepath);
            remove_metadata_from_file(filepath, cleaned_filepath);
        }

        close(client_socket);
        pthread_exit(NULL);
    }

    close(client_socket);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "사용법: %s <IP 주소> <포트 번호>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *ip = argv[1];
    const char *port = argv[2];

    ensure_upload_dir(); // UPLOAD_DIR 생성 확인 및 생성

    replace_action_url(ip, port);

    char url[256];
    snprintf(url, sizeof(url), "http://%s:%s", ip, argv[2]);
    printf("생성된 URL: %s\n", url);

    print_qr_code(url);

    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(atoi(port));

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %s...\n", port);

    while (1) {
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (*client_socket < 0) {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_socket);
        pthread_detach(thread);
    }

    close(server_socket);
    return 0;
}

