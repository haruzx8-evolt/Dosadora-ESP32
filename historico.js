// Histórico de dosagens - persistido no navegador (localStorage)
const CHAVE_HISTORICO = "dosadora_historico";
const LIMITE_REGISTROS = 50;

function obterHistorico() {
  try {
    return JSON.parse(localStorage.getItem(CHAVE_HISTORICO)) || [];
  } catch {
    return [];
  }
}

function registrarDosagem(meta, pesoFinal) {
  const historico = obterHistorico();
  historico.push({
    data: new Date().toISOString(),
    meta: meta,
    pesoFinal: Number(pesoFinal.toFixed(1)),
  });
  if (historico.length > LIMITE_REGISTROS) historico.shift();
  localStorage.setItem(CHAVE_HISTORICO, JSON.stringify(historico));
}

function limparHistorico() {
  localStorage.removeItem(CHAVE_HISTORICO);
}
