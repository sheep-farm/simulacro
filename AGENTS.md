# AGENTS.md — regras para agentes (OpenCode etc.) neste repositório

## Regra de validação visual (gravada a pedido do mantenedor, 2026-11-11)

**NUNCA tire prints de tela (screenshots, `import -window`, X11 dumps etc.)
para avaliar o estado visual do app.** Quando uma mudança precisar de
validação visual:

1. pergunte ao mantenedor antes;
2. entregue o comando para abrir o app (e o arquivo/roteiro de verificação);
3. aguarde o mantenedor avaliar e retornar o veredito.

O que NÃO cai na regra: builds, `make test`, traces headless (`--run`),
dumps numéricos/CSV, logs — esses seguem no fluxo normal de teste.

Contexto: a validação visual por screenshots provou-se frágil e consumidora
de tempo (IDs de janela, coordenadas, estados de foco). O mantenedor prefere
ser o avaliador humano dessa etapa.

## Regras do projeto

- Contratos congelados por testes: formato `nodesim 10` (arquivos **.sim**;
  o loader ainda lê .nodesim legados), ABI 14, `NODESIM_*` envs. Nada de
  bump de ABI sem decisão explícita.
- `make test` verde antes de commit; goldens só se regeneram com intenção
  documentada (ver README, seção *Regression discipline*).
- Cada camada gateada (`SIMULACRO_NODE_UI`, `SIMULACRO_NODE_WINDOW`) precisa
  compilar em TODAS as configs — a matrix está em `tests/run.sh`
  (`syntax_check`); não desligue-a.
- Comentários/decisões em pt-BR estilo mantenedor; rótulos de código em
  inglês; i18n para strings de UI.
