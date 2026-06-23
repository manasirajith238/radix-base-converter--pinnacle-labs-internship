FROM gcc:13
WORKDIR /app
COPY . .
RUN g++ -O2 -std=c++17 -o server server.cpp
EXPOSE 8080
CMD ["./server", "8080"]