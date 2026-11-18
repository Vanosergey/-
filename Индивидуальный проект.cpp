// ============================================================================
// ПРОЕКТ: Умная парковка
// ФАЙЛ: main.cpp
// ОПИСАНИЕ: Система мониторинга парковочных мест с двумя режимами.
// ============================================================================

#include <opencv2/opencv.hpp>  // Библиотека компьютерного зрения OpenCV
#include <iostream>            // Для вывода в консоль (cout, cerr)
#include <fstream>             // Для работы с файлами (чтение/запись)
#include <vector>              // Для массивов (vector)
#include <string>              // Для работы со строками

using namespace cv;
using namespace std;

// Структура для хранения данных парковочного места
struct ParkingSpot {
    int id;               // Номер места
    Rect rect;            // Координаты и размер зоны
    bool occupied;        // Статус (true - занято, false - свободно)
};

// Глобальные переменные для работы мыши в редакторе
vector<Rect> zones;
bool isDrawing = false;
Point startPoint;
Mat img, imgCopy;

// ============================================================================
// Обработчик событий мыши (используется только в режиме разметки)
// ============================================================================
void onMouse(int event, int x, int y, int flags, void* param) {
    // Нажатие ЛКМ - запоминаем точку старта
    if (event == EVENT_LBUTTONDOWN) {
        isDrawing = true;
        startPoint = Point(x, y);
    }
    // Движение мыши с зажатой кнопкой - рисуем "резиновую" рамку
    else if (event == EVENT_MOUSEMOVE && isDrawing) {
        imgCopy = img.clone();
        rectangle(imgCopy, startPoint, Point(x, y), Scalar(0, 255, 0), 2);

        // Перерисовываем уже сохраненные рамки, чтобы они не исчезали
        for (size_t i = 0; i < zones.size(); ++i) {
            rectangle(imgCopy, zones[i], Scalar(0, 255, 0), 2);
            putText(imgCopy, to_string(i + 1), Point(zones[i].x + 5, zones[i].y + 20), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 0), 2);
        }
        imshow("Zone Editor", imgCopy);
    }
    // Отпускание ЛКМ - фиксируем зону
    else if (event == EVENT_LBUTTONUP) {
        isDrawing = false;
        Rect newRect(startPoint, Point(x, y));

        // Игнорируем слишком маленькие рамки (защита от случайных кликов)
        if (newRect.width > 10 && newRect.height > 10) {
            zones.push_back(newRect);
        }

        // Перерисовываем кадр с добавленной зоной
        imgCopy = img.clone();
        for (size_t i = 0; i < zones.size(); ++i) {
            rectangle(imgCopy, zones[i], Scalar(0, 255, 0), 2);
            putText(imgCopy, to_string(i + 1), Point(zones[i].x + 5, zones[i].y + 20), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 0), 2);
        }
        imshow("Zone Editor", imgCopy);
    }
}

// ============================================================================
// Чтение зон из текстового файла
// ============================================================================
vector<ParkingSpot> loadZones(const string& path) {
    vector<ParkingSpot> spots;
    ifstream f(path);
    if (!f.is_open()) {
        cerr << "ОШИБКА: Не открыт файл " << path << endl;
        return spots;
    }
    int id, x, y, w, h;
    while (f >> id >> x >> y >> w >> h) {
        spots.push_back({ id, Rect(x, y, w, h), false });
    }
    return spots;
}

// ============================================================================
// Главная функция
// ============================================================================
int main(int argc, char** argv) {
    // Отключаем многопоточность OpenCV (предотвращает вылеты в режиме Debug)
    cv::setNumThreads(0);

    string rtsp_url = "rtsp://123456:Artem2017@192.168.2.122:554/stream1";

    // Проверяем аргумент командной строки (наличие флага --edit)
    bool editMode = (argc > 1 && string(argv[1]) == "--edit");

    if (editMode) {
        // ====================================================================
        // РЕЖИМ 1: Разметка зон (редактор)
        // ====================================================================
        cout << "=== РЕЖИМ: РАЗМЕТКА ЗОН ===" << endl;

        VideoCapture cap(rtsp_url);
        if (!cap.isOpened()) {
            cout << "ОШИБКА: Камера не открылась." << endl;
            return -1;
        }

        // Получаем один кадр с камеры и отключаемся от потока
        cap >> img;
        cap.release();

        if (img.empty()) {
            cout << "ОШИБКА: Получен пустой кадр с камеры." << endl;
            return -1;
        }

        imgCopy = img.clone();
        namedWindow("Zone Editor", WINDOW_AUTOSIZE);
        setMouseCallback("Zone Editor", onMouse, 0);
        imshow("Zone Editor", imgCopy);

        // Ждем нажатия ESC
        waitKey(0);

        // Сохраняем результат в файл
        ofstream f("zones.txt");
        if (f.is_open()) {
            for (size_t i = 0; i < zones.size(); ++i) {
                f << (i + 1) << " " << zones[i].x << " " << zones[i].y
                    << " " << zones[i].width << " " << zones[i].height << endl;
            }
            f.close();
            cout << "Сохранено мест: " << zones.size() << endl;
        }
        else {
            cout << "ОШИБКА: Не удалось сохранить файл." << endl;
        }

    }
    else {
        // ====================================================================
        // РЕЖИМ 2: Основной детектор (мониторинг)
        // ====================================================================
        cout << "=== РЕЖИМ: МОНИТОРИНГ ПАРКОВКИ ===" << endl;

        vector<ParkingSpot> spots = loadZones("zones.txt");
        if (spots.empty()) {
            cout << "ОШИБКА: Файл зон пуст. Запустите программу с флагом --edit." << endl;
            return -1;
        }

        VideoCapture cap(rtsp_url);
        if (!cap.isOpened()) {
            cout << "ОШИБКА: Камера не открылась." << endl;
            return -1;
        }

        Mat frame, gray;

        while (true) {
            cap >> frame;
            if (frame.empty()) {
                cout << "ВНИМАНИЕ: Поток с камеры прервался." << endl;
                break;
            }

            // Перевод в оттенки серого для ускорения обработки
            cvtColor(frame, gray, COLOR_BGR2GRAY);

            int freeCount = 0; // Счетчик свободных мест

            for (auto& s : spots) {
                // Защита от выхода за границы кадра
                if (s.rect.x + s.rect.width > frame.cols || s.rect.y + s.rect.height > frame.rows) {
                    continue;
                }

                Mat roi = gray(s.rect);
                Mat edges;
                // Детектор границ Кэнни: ищем перепады яркости (детали машины)
                Canny(roi, edges, 50, 150);

                int edgeCount = countNonZero(edges);

                // Если границ больше 5% от площади зоны, считаем место занятым
                s.occupied = (edgeCount > (s.rect.area() * 0.05));

                if (!s.occupied) freeCount++;

                // Отрисовка: красный - занято, зеленый - свободно
                Scalar color = s.occupied ? Scalar(0, 0, 255) : Scalar(0, 255, 0);
                rectangle(frame, s.rect, color, 2);

                // Текст на английском (кириллица в OpenCV не отображается)
                string label = to_string(s.id) + (s.occupied ? " (busy)" : " (free)");
                putText(frame, label, Point(s.rect.x + 5, s.rect.y + 20),
                    FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
            }

            // === СОХРАНЕНИЕ СТАТУСА В ФАЙЛ ===
            // Записываем общее количество свободных мест (для будущего бота)
            ofstream statusFile("status.txt");
            if (statusFile.is_open()) {
                statusFile << freeCount;
                statusFile.close();
            }

            imshow("Smart Parking", frame);

            if (waitKey(1) == 27) break;
        }
    }
    return 0;
}
