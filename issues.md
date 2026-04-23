-    * Consulta Direta via Telnet (query_ls_nowplaying):
       * Por que está errado: O cérebro não deve "escanear" o rádio. Ele deve receber o estado pronto.
       * Destino: O SmoothOperator deve manter o estado atual do Liquidsoap em cache e fornecê-lo via API/RMQ.


- Smoothoperator deve manter o estado do que tá tocando.
como ele vai fazer isso? pooling 5s, monitorar queues?

  O que o BroadcastManager (Operador) deve prover para o DJ?

  Para o Jax ser esse DJ empoderado, o BroadcastManager tem que oferecer uma "Mesa de Som" limpa:
   * Comando: carregar_lista(nome)
   * Comando: falar(arquivo_audio)
   * Comando: pular()
   * Notificação: "DJ, a música terminou, o que eu faço agora?"

       -  RabbitMQ (Assíncrono): É excelente para comandos de "ação" (ex: "pule a música", "mude o volume"). Você envia a
      mensagem e a rádio processa quando puder. Mas ele não devolve uma resposta imediata para o terminal.
       * Problema: Se o show playing usasse apenas RabbitMQ, ele enviaria a pergunta e o script terminaria antes da
         rádio responder. O usuário ficaria sem ver nada na tela.

- State Polling: The producer correctly polls Liquidsoap and
     publishes state updates (e.g., state.current_song),
     although it occasionally encounters publishing errors that
     may require further investigation into RabbitMQ exchange
     stability.


- why it is using smootoperator.env and not smootoperator.json, .env is only for secrets.


- adicionar o suporte ao evento playlist.set_uri no
  SmoothOperator.
 Enviar evento playlist.set_uri com a nova rota.
   2. Enviar evento playlist.reload.

- .env file template repeating information that is conf.json. To check where the application should consume it. everuthing that is not secret, like password and api must be comming from config file. (smothoperator.json)



  ┌──────────────────────┬─────────────────────┬──────────────────────────────────────────────────────────────────┐
  │ Camada               │ Componente          │ Responsabilidade                                                 │
  ├──────────────────────┼─────────────────────┼──────────────────────────────────────────────────────────────────┤
  │ Use Cases            │ Jax Silver (Lógica) │ "Decidi que agora deve tocar música clássica."                   │
  │ Interface Adapters   │ SmoothOperator      │ "Vou traduzir esse desejo para o comando técnico playlist.load." │
  │ Frameworks / Drivers │ Liquidsoap          │ O motor que realmente toca o arquivo de áudio.                   │
  └──────────────────────┴─────────────────────┴──────────────────────────────────────────────────────────────────┘


  o sistema atual está
  tentando usar o RabbitMQ como se fosse uma conexão de rede direta (Request/Response), quando ele brilha mesmo é no
  modelo de Eventos e Estados.

  Para o SmoothOperator funcionar de forma "Clean", ele deve usar o Pub/Sub do RabbitMQ para implementar um Global State
  (Estado Global).

  Como aplicar o Pub/Sub para resolver as informações:


    1. A Exchange de Estado (O "Sub")
  O SmoothOperator deve publicar em uma exchange do tipo fanout ou topic (ex: radio.status) toda vez que algo mudar:
   * Música mudou? Publica.
   * Playlist terminou? Publica.
   * Volume mudou? Publica.


    O Papel do SmoothOperator como "Publisher"

  Para isso funcionar, o SmoothOperator (o Gateway) precisa ser proativo:
   * Ele monitora o Liquidsoap.
   * Ele empacota o estado completo:

   1     {
   2       "status": "playing",
   3       "track": {"artist": "...", "title": "..."},
   4       "playlist": "80s_rock",
   5       "position": 120.5
   6     }
   * Ele faz o Broadcast disso via Pub/Sub.

  E se um cliente entrar "no meio" da música?
  No RabbitMQ, podemos usar o conceito de "Last Value Cache" ou simplesmente fazer o Jax Silver (ao conectar) enviar uma
  única mensagem de request_status, e o SmoothOperator responde via Pub/Sub para todos, garantindo que o novo cliente
  sincronize.

  Faz sentido? Com o Pub/Sub, o Jax Silver para de "interagir" com o rádio e passa a "reagir" ao rádio. É a inversão de
  controle clássica da Clean Architecture.


    2. No SmoothOperator (O Gateway/Adapter)
  Quando o SmoothOperator detecta o evento de troca de música, ele deve enriquecer a mensagem antes de publicá-la no
  RabbitMQ.

  O SmoothOperator faria o seguinte:
   1. Recebe o evento do Liquidsoap.
   2. Lê o metadado duration.
   3. Captura o timestamp atual do sistema (este é o seu start_time).
   4. Calcula o end_time previsto (start_time + duration).

  3. O Payload JSON no Pub/Sub
  O Jax Silver receberia uma mensagem assim pelo RabbitMQ:

    1 {
    2   "event": "track.changed",
    3   "payload": {
    4     "artist": "Aerosmith",
    5     "title": "Dream On",
    6     "duration": 268.0,
    7     "start_time": "2026-04-23T14:00:00Z",
    8     "end_time": "2026-04-23T14:04:28Z",
    9     "playlist": "Classic_Rock"
   10   }
   11 }

  Para manter o SmoothOperator eficiente em C++, o foco é definir a ABI de comunicação (o contrato JSON) que ele vai
  usar para servir o Jax Silver.

  Já fizemos o primeiro passo: atualizamos o radio.liq. O Liquidsoap já está cuspindo o tempo. O SmoothOperator agora
  precisa ser "ensinado" (via atualização no código C++ ou configuração) a capturar esses novos campos e gerenciar o
  Pub/Sub de Estado.

 Mapeamento do SmoothOperator (Versão C++ Renovada)

  Aqui está como a estrutura que você propôs se traduz para a realidade do C++:

   1 smoothoperator (C++) /
   2 ├── src/
   3 │   ├── core/                # Lógica de Sincronização (State Manager)
   4 │   │   ├── radio_state.hpp  # Estrutura do "Estado da Verdade"
   5 │   │   └── time_engine.cpp  # Cálculos de UTC e Duração
   6 │   ├── drivers/             # Interfaces de Baixo Nível
   7 │   │   ├── liquidsoap_client.cpp # Telnet/Unix Socket puro
   8 │   │   └── rabbitmq_adapter.cpp  # Pub/Sub de alta performance (libamqp-cpp ou similar)
   9 │   └── main.cpp             # Event Loop (Libev ou Asio)
