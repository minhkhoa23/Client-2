// Client.cpp

#include "afxsock.h"
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <vector>
#include <sstream>
#include <unordered_map>

#define BUFFER_SIZE 1024

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace std;

CWinApp theApp;
bool running = true;

void ShowCur(bool CursorVisibility) {
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursor = { 1, CursorVisibility };
    SetConsoleCursorInfo(handle, &cursor);
}

void signalHandler(int signum) {
    cout << "Interrupt signal (" << signum << ") received. Shutting down client...\n";
    running = false;
}

// Load downloaded files from a text file
vector<string> loadDownloadedFiles(const char* fileName) {
    ifstream file(fileName);
    vector<string> downloadedFiles;
    string line;

    while (getline(file, line)) {
        downloadedFiles.push_back(line);
    }

    file.close();
    return downloadedFiles;
}

// Save a downloaded file name
void saveDownloadedFile(const char* fileName, const string& downloadedFile) {
    ofstream file(fileName, ios::app);
    if (file.is_open()) {
        file << downloadedFile << endl;
        file.close();
    }
    else {
        cerr << "Error opening file: " << fileName << endl;
    }
}

// Get new files to download from input file
vector<pair<string, string>> getNewFilesToDownload(const char* inputFileName, const vector<string>& downloadedFiles) {
    ifstream file(inputFileName);
    vector<pair<string, string>> newFiles;
    string line;

    if (downloadedFiles.size() > 0) {
        while (getline(file, line)) {
            bool isDownloaded = false;
            for (const auto& downloadedFile : downloadedFiles) {
                if (line.find(downloadedFile) != string::npos) {
                    isDownloaded = true;
                    break;
                }
            }

            if (!isDownloaded) {
                istringstream ss(line);
                string fileName, priority;
                ss >> fileName >> priority;
                newFiles.push_back({ fileName, priority });
            }
        }
    }
    else {
        while (getline(file, line)) {
            istringstream ss(line);
            string fileName, priority;
            ss >> fileName >> priority;
            newFiles.push_back({ fileName, priority });
        }
    }

    file.close();
    return newFiles;
}

struct Chunk {
    char buffer[BUFFER_SIZE];
    string status;
    string fileName;
};

// Download file from server
void downloadFile(CSocket& serverSocket, vector<pair<string, string>>& downloadQueue) {
    unordered_map<string, ofstream> fileStreams;
    unordered_map<string, long long> fileSizes;
    unordered_map<string, long long> bytesReceived;

    int currentIndex = 0;

    while (running) {
        if (downloadQueue.empty()) continue;

        // Determine the number of chunks to request based on priority
        string currentFile = downloadQueue[currentIndex].first;
        string priority = downloadQueue[currentIndex].second;
        int numChunks = (priority == "CRITICAL") ? 10 : (priority == "HIGH") ? 4 : 1;

        // Send request to server
        string request = currentFile + " " + priority;
        serverSocket.Send(request.c_str(), request.size());

        // Receive data from server
        Chunk chunk;
        int bytesReceivedNow;
        while ((bytesReceivedNow = serverSocket.Receive(reinterpret_cast<char*>(&chunk), sizeof(chunk))) > 0) {
            if (chunk.status == "BEGN") {
                // Begin chunk
                istringstream header(chunk.buffer);
                string fileName, fileSizeStr;
                header >> fileName >> fileSizeStr;

                long long fileSize = stoll(fileSizeStr);
                fileSizes[fileName] = fileSize;
                bytesReceived[fileName] = 0;

                string outputPath = "output/" + fileName;
                fileStreams[fileName].open(outputPath, ios::binary);
                if (!fileStreams[fileName].is_open()) {
                    cerr << "Error creating file to save the download" << endl;
                    return;
                }
                cout << "Starting download of " << fileName << " (" << fileSizeStr << " bytes)\n";
            }
            else if (chunk.status == "DATA") {
                // Data chunk
                string fileName = chunk.fileName;
                if (fileStreams.find(fileName) == fileStreams.end()) continue;

                fileStreams[fileName].write(chunk.buffer, bytesReceivedNow - 4);
                bytesReceived[fileName] += bytesReceivedNow - 4;
                cout << "\rDownloading " << fileName << " .... " << (bytesReceived[fileName] * 100) / fileSizes[fileName] << " %";
            }
            else if (chunk.status == "END ") {
                // End chunk
                string fileName = chunk.fileName;
                if (fileStreams.find(fileName) != fileStreams.end()) {
                    fileStreams[fileName].close();
                    cout << "\nFinished downloading " << fileName << endl;
                    saveDownloadedFile("downloaded_files.txt", fileName);
                    fileStreams.erase(fileName);
                    fileSizes.erase(fileName);
                    bytesReceived.erase(fileName);
                }
                break;
            }
            else if (chunk.status == "ERRO") {
                cerr << "\nError: File not found or other error." << endl;
                break;
            }
        }

        // Move to the next file in the download queue
        currentIndex = (currentIndex + 1) % downloadQueue.size();
    }

    // Close all open file streams
    for (auto& fs : fileStreams) {
        fs.second.close();
    }
}

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[]) {
    ShowCur(false);
    signal(SIGINT, signalHandler);
    int nRetCode = 0;
    if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0)) {
        _tprintf(_T("Fatal Error: MFC initialization failed\n"));
        nRetCode = 1;
    }
    else {
        if (AfxSocketInit() == false) {
            cout << "Unable to initialize Socket Library " << endl;
            return 1;
        }

        CSocket clientSocket;
        if (!clientSocket.Create()) {
            cout << "Unable to create socket " << endl;
            return 1;
        }

        if (clientSocket.Connect(_T("127.0.0.1"), 1234) != 0) {
            cout << "Connected to server successfully!!" << endl << endl;

            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);
            clientSocket.Receive(buffer, BUFFER_SIZE);
            cout << "Available files from server: \n" << buffer << endl;

            vector<string> downloadedFiles = loadDownloadedFiles("downloaded_files.txt");

            // Load the download queue
            vector<pair<string, string>> downloadQueue = getNewFilesToDownload("input.txt", downloadedFiles);

            // Start download thread
            thread downloadThread(downloadFile, ref(clientSocket), ref(downloadQueue));

            // Keep the main thread running
            while (running) {
                this_thread::sleep_for(chrono::seconds(2));
            }

            downloadThread.join();
        }
        else {
            cout << "Unable to connect to server " << endl;
        }

        clientSocket.Close();
        cout << "Client closed!" << endl;
    }
    return nRetCode;
}
