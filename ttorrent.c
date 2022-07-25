#include "file_io.h"

#include "logger.h"

#include "string.h"

#include <sys/socket.h>

#include <arpa/inet.h>

#include <stdlib.h>

#include <netinet/in.h>

#include <unistd.h>

#include <poll.h>

#include <assert.h>

#include <ctype.h>

static const uint32_t MAGIC_NUMBER = 0xde1c3232; // = htonl(0x32321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum
{
  RAW_MESSAGE_SIZE = 13,
  MAX_FILENAME_BYTES = 255
};

struct clientData
{
  uint8_t data[RAW_MESSAGE_SIZE];
  uint64_t recivedData;
};

/**
 * Main function.
 */
int main(int argc, char **argv)
{

  if (argc == 2)
  {

    set_log_level(LOG_DEBUG);

    log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "Ismael Crespo Sagrado y Gonzalo García Navarro");

    if(strlen(argv[1]) > MAX_FILENAME_BYTES){
      perror("Path demasiado largo");
      exit(0);
    }

    const char *extension = strrchr(argv[1], '.') + 1;
    if(strcmp(extension, "ttorrent")){
      perror("La extensión debe ser .ttorrent");
      exit(0);
    }

    struct torrent_t torrent;

    if (create_torrent_from_metainfo_file(argv[1], &torrent, "torrent_samples/client/test_file") == -1)
    {
      perror("ERROR AL CARGAR TORRENT: ");
    }

    assert(&torrent != NULL);

    for (uint64_t i = 0; i < torrent.peer_count; i++)
    {

      const int sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock == -1)
      {
        perror("ERROR CREACIÓN SOCKET");
        exit(0);
      }

      struct sockaddr_in serverAddress;
      serverAddress.sin_family = AF_INET;

      char bufferIp[16];

      sprintf(bufferIp, "%d.%d.%d.%d", torrent.peers[i].peer_address[0], torrent.peers[i].peer_address[1], torrent.peers[i].peer_address[2], torrent.peers[i].peer_address[3]);
      serverAddress.sin_addr.s_addr = inet_addr(bufferIp);


      serverAddress.sin_port = torrent.peers[i].peer_port;

      if (connect(sock, (const struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1)
      {
        perror("ERROR EN LA CONEXIÓN: ");
        continue;
      }

      for (uint64_t j = 0; j < torrent.block_count; j++)
      {
        if (torrent.block_map[j] == 0)
        {
          uint8_t requestBuffer[RAW_MESSAGE_SIZE];
          requestBuffer[0] = (uint8_t)(MAGIC_NUMBER >> 24) & 0xff;
          requestBuffer[1] = (uint8_t)(MAGIC_NUMBER >> 16) & 0xff;
          requestBuffer[2] = (uint8_t)(MAGIC_NUMBER >> 8) & 0xff;
          requestBuffer[3] = (uint8_t)(MAGIC_NUMBER >> 0) & 0xff;
          requestBuffer[4] = MSG_REQUEST;
          requestBuffer[5] = (uint8_t)(j >> 56) & 0xff;
          requestBuffer[6] = (uint8_t)(j >> 48) & 0xff;
          requestBuffer[7] = (uint8_t)(j >> 40) & 0xff;
          requestBuffer[8] = (uint8_t)(j >> 32) & 0xff;
          requestBuffer[9] = (uint8_t)(j >> 24) & 0xff;
          requestBuffer[10] = (uint8_t)(j >> 16) & 0xff;
          requestBuffer[11] = (uint8_t)(j >> 8) & 0xff;
          requestBuffer[12] = (uint8_t)(j >> 0) & 0xff;

          if (send(sock, requestBuffer, RAW_MESSAGE_SIZE, 0) == -1)
          {
            perror("ERROR AL ENVIAR REQUEST: ");
            exit(0);
          }

          if (recv(sock, requestBuffer, RAW_MESSAGE_SIZE, MSG_WAITALL) == -1)
          {
            perror("ERROR AL REC RESPONSE BLOQUE: ");
            exit(0);
          }

          if (requestBuffer[4] == MSG_RESPONSE_OK)
          {

            struct block_t block;
            block.size = get_block_size(&torrent, j);

            if (recv(sock, block.data, block.size, MSG_WAITALL) == -1)
            {
              perror("ERROR AL REC BLOQUE: ");
              exit(0);
            }

            if (store_block(&torrent, j, &block) == -1)
            {
              perror("ERROR AL GUARDAR BLOQUE: ");
              exit(0);
            }
          }
          else
          {
            perror("BLOQUE NO DISPONIBLE ");
            exit(0);
          }
        }
      }

      if (close(sock) == -1)
      {
        perror("ERROR AL CERRAR EL SOCKET ");
        exit(0);
      }

      uint64_t correctBlocks = 0;
      for (uint64_t d = 0; d < torrent.block_count; d++)
      {
        correctBlocks += torrent.block_map[d];
      }

      if (correctBlocks == torrent.block_count)
      {
        break;
      }
    }

    if (destroy_torrent(&torrent) == -1)
    {
      perror("ERROR AL DESTRUIR EL TORRENT ");
      exit(0);
    }
  }
  else if (argc == 4)
  {
    if(strcmp(argv[1], "-l")){
      perror("¿Quizá quiso decir -l ?");
      exit(0);
    }

    //En el caso de que nos intenten pasar por parámetro un puerto que no sea un dígito lanzamos error
    for (size_t u = 0; u < strlen(argv[2]); u++)
    {
      if(!isdigit((argv[2][u]))){
        perror("Introduce un puerto dónde solo consten dígitos.");
        exit(0);
      }
    }

    // En caso de que el path sea mas largo que 255 chars
    if(strlen(argv[3]) > MAX_FILENAME_BYTES){
      perror("Path demasiado largo");
      exit(0);
    }

    //La extensión debe estár correcta
    const char *extension = strrchr(argv[3], '.') + 1;
    if(strcmp(extension, "ttorrent")){
      perror("La extensión debe ser .ttorrent");
      exit(0);
    }

    struct torrent_t torrent;
    
    if (create_torrent_from_metainfo_file(argv[3], &torrent, "torrent_samples/server/test_file_server") == -1)
    {
      perror("ERROR AL CARGAR TORRENT: ");
      exit(0);
    }

    assert(&torrent != NULL);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
      perror("ERROR CREACIÓN SOCKET");
      exit(0);
    }

    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htons(INADDR_ANY);
    serverAddress.sin_port = htons((uint16_t)atoi(argv[2]));

    if (bind(sock, (const struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1)
    {
      perror("ERROR EN EL BIND");
      exit(0);
    }

    int fd_count = 0;  // Para saber la siguiente posición del array en la que añadiremos un cliente (fd)
    int fd_size = 200; // Número de estructuras máximas que representan a los clientes.
    struct pollfd *pfds = malloc(sizeof *pfds * (nfds_t)fd_size);
    if(pfds == NULL){
      perror("ERROR EN MALLOC: ");
      exit(0);
    }

    struct clientData *auxClientData = malloc(sizeof(*auxClientData) * (nfds_t)fd_size);
    if(auxClientData == NULL){
      perror("ERROR EN MALLOC: ");
      exit(0);
    }

    if (listen(sock, 10) == -1)
    {
      perror("ERROR EN LISTEN");
      exit(0);
    }

    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    fd_count = 1;

    struct sockaddr_storage clientAddr;
    int newClientFd;
    socklen_t clientAddrL = sizeof clientAddr;

    while (1)
    {

      if (poll(pfds, (nfds_t)fd_count, -1) == -1)
      {
        perror("ERROR AL INICIAR POLL: ");
        exit(0);
      }

      for (int i = 0; i < fd_count; i++)
      {

        if (pfds[i].revents & POLLIN)
        {

          if (pfds[i].fd == sock)
          {
            // Si el fd es el del servidor quiere decir que está listo para escuchar a un cliente.
            log_printf(LOG_INFO, "Aceptamos un cliente");
            newClientFd = accept(sock, (struct sockaddr *)&clientAddr, &clientAddrL);
            if (newClientFd == -1)
            {
              continue;
            }

            if (fd_count == fd_size)
            {
              fd_size *= 2;
              pfds = realloc(pfds, sizeof(*pfds) * (nfds_t)fd_size);
              if(pfds == NULL){
                perror("ERROR EN REALLOC PFDS: ");
                exit(0);
              }

              auxClientData = realloc(auxClientData, sizeof(*auxClientData) * (nfds_t)fd_size);
              if(auxClientData == NULL){
                perror("ERROR EN REALLOC AUXCLIENTDATA: ");
                exit(0);
              }
            }

            // Añadimos el nuevo cliente (en la siguiente posición) al array de structs
            // Sumamos uno al contador de fd para reflejar esta acción (para que "apunte" al siguiente libre)
            pfds[fd_count].fd = newClientFd;
            pfds[fd_count].events = POLLIN;
            auxClientData[fd_count].recivedData = 0;
            fd_count++;
          }
          else
          {
            // Si no es el fd del servidor quiere decir que es el de un cliente que está listo para leer/enviar datos.

            uint8_t requestBuffer[RAW_MESSAGE_SIZE];
            ssize_t recvBytes = recv(pfds[i].fd, requestBuffer, RAW_MESSAGE_SIZE, 0);

            if (recvBytes <= 0)
            {
              // En cualquier caso quitamos al cliente para no estar mirando clientes que cerraron el socket o si falla el rcv
              if(close(pfds[i].fd)){
                perror("ERROR AL CERRAR EL FD");
                continue;
              }
              pfds[i] = pfds[fd_count - 1]; // Ponemos el del final en la posición actual y el del final ya se substituye por el siguiente que entre.
              auxClientData[i] = auxClientData[fd_count - 1];
              fd_count--;
              continue;
            }

            if (auxClientData[fd_count].recivedData + ((uint64_t)recvBytes) > 13)
            {
              // el cliente ha enviado algo raro, se continua la ejecución ignorando a este cliente
              continue;
            }

            
            // Rellenamos el buffer temporal del request.
            for (uint64_t z = 0; z < ((uint64_t)recvBytes); z++)
            {
              // Empezamos a introducir datos desde donde nos quedamos
              auxClientData[i].data[auxClientData[i].recivedData + z] = requestBuffer[z];
            }

            // Si recibimos menos más bytes que 0 y menos que 13 lo reflejamos en la variable.
            auxClientData[i].recivedData += (uint64_t)recvBytes;
            log_printf(LOG_INFO, "%d", auxClientData[i].recivedData);

            if (auxClientData[i].recivedData != 13)
            {
              // Si aún no tenemos los 13 bytes del request continua ejecución
              continue;
            }

            auxClientData[i].recivedData = 0;

            // MAGIC NUMBER
            uint32_t REQ_MAGIC_NUMBER = (uint32_t)auxClientData[i].data[0] << 24;
            REQ_MAGIC_NUMBER |= (uint32_t)(auxClientData[i].data[1] << 16);
            REQ_MAGIC_NUMBER |= (uint32_t)(auxClientData[i].data[2] << 8);
            REQ_MAGIC_NUMBER |= (uint32_t)(auxClientData[i].data[3]);

            // NÚMERO DE BLOQUE
            uint64_t REQ_NUM_BLOCK = (uint64_t)auxClientData[i].data[5] << 56;
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[6] << 48);
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[7] << 40);
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[8] << 32);
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[9] << 24);
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[10] << 16);
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[11] << 8);
            REQ_NUM_BLOCK |= ((uint64_t)auxClientData[i].data[12]);


            if ((torrent.block_map[REQ_NUM_BLOCK] == 1) && (REQ_MAGIC_NUMBER == MAGIC_NUMBER))
            {

              struct block_t block;
              block.size = get_block_size(&torrent, REQ_NUM_BLOCK);

              if (load_block(&torrent, REQ_NUM_BLOCK, &block) == -1)
              {
                perror("ERROR AL CARGAR BLOQUE: ");
                continue; // Posible mejora continuar execution
              }
              
              assert(&block != NULL);

              uint8_t responseBuffer[RAW_MESSAGE_SIZE + block.size];

              for (int k = 0; k < RAW_MESSAGE_SIZE; k++)
              {
                responseBuffer[k] = auxClientData[i].data[k];
              }

              responseBuffer[4] = MSG_RESPONSE_OK;

              for (uint64_t j = 0; j < block.size; j++)
              {
                responseBuffer[j + RAW_MESSAGE_SIZE] = block.data[j];
              }

              if (send(pfds[i].fd, responseBuffer, RAW_MESSAGE_SIZE + block.size, 0) == -1)
              {
                perror("ERROR AL ENVIAR RESPONSE: ");
                continue;
              }
            }
            else
            {
              // Bloque no disponible
              auxClientData[i].data[4] = MSG_RESPONSE_NA;
              if (send(pfds[i].fd, auxClientData[i].data, RAW_MESSAGE_SIZE, 0) == -1)
              {
                perror("ERROR AL ENVIAR REQUEST: ");
                continue;
              }
            }
          }

        } // FD LISTO PARA USAR
      }   // BUCLE FOR DE LOS CLIENTES
    }     // BUCLE INFINITO SERVER

    if (destroy_torrent(&torrent) == -1)
    {
      perror("ERROR AL DESTRUIR EL TORRENT ");
      exit(0);
    }

    return 0;
  }
}
