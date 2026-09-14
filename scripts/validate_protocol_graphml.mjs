import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const lapki = path.resolve(repository, '../lapki-client');
const require = createRequire(import.meta.url);
const { parseCGML } = require(
  path.join(lapki, 'node_modules/@kruzhok-team/cyberiadaml-js/dist/index.js')
);

const graphml = fs.readFileSync(path.join(repository, 'Скетч.graphml'), 'utf8');
const parsed = parseCGML(graphml);
const platformFiles = {
  ArduinoUno: 'ArduinoUno-1.0.json',
  'tjc-ms1-main-a4': 'tjc-ms1-main-a4-1.0.json',
  'tjc-ms1-ard-a1': 'tjc-ms1-ard-a1-1.0.json',
};

function invariant(condition, message) {
  if (!condition) throw new Error(message);
}

function splitArguments(source) {
  if (!source.trim()) return [];
  return source.split(',').map((item) => item.trim());
}

function validateAction(source, machine, platform, location) {
  for (const line of source.split('\n').map((item) => item.trim()).filter(Boolean)) {
    const match = /^([A-Za-z][A-Za-z0-9_]*)\.([A-Za-z][A-Za-z0-9_]*)\((.*)\);$/.exec(line);
    invariant(match, `${location}: действие не разобрано: ${line}`);
    const [, componentId, methodName, rawArguments] = match;
    const component = Object.values(machine.components).find((item) => item.id === componentId);
    invariant(component, `${location}: неизвестный компонент ${componentId}`);
    const prototype = platform.components[component.type];
    invariant(prototype, `${location}: тип ${component.type} отсутствует в платформе`);
    const method = prototype.methods[methodName];
    invariant(method, `${location}: у ${component.type} нет метода ${methodName}`);
    const argumentsList = splitArguments(rawArguments);
    const parameters = method.parameters ?? [];
    const required = parameters.filter((item) => !item.optional).length;
    invariant(
      argumentsList.length >= required && argumentsList.length <= parameters.length,
      `${location}: ${componentId}.${methodName} получил ${argumentsList.length} аргументов, ожидалось ${required}..${parameters.length}`
    );
  }
}

function validateTrigger(trigger, machine, platform, location) {
  if (!trigger || trigger === 'entry' || trigger === 'exit') return;
  const match = /^([A-Za-z][A-Za-z0-9_]*)\.([A-Za-z][A-Za-z0-9_]*)$/.exec(trigger);
  invariant(match, `${location}: событие не разобрано: ${trigger}`);
  const component = Object.values(machine.components).find((item) => item.id === match[1]);
  invariant(component, `${location}: неизвестный компонент события ${match[1]}`);
  const signal = platform.components[component.type]?.signals[match[2]];
  invariant(signal, `${location}: у ${component.type} нет сигнала ${match[2]}`);
}

function validateOperand(operand, machine, platform, location) {
  if (/^-?\d+$/.test(operand)) return;
  const match = /^([A-Za-z][A-Za-z0-9_]*)\.([A-Za-z][A-Za-z0-9_]*)$/.exec(operand);
  invariant(match, `${location}: операнд условия не разобран: ${operand}`);
  const component = Object.values(machine.components).find((item) => item.id === match[1]);
  invariant(component, `${location}: неизвестный компонент условия ${match[1]}`);
  invariant(
    platform.components[component.type]?.variables[match[2]],
    `${location}: у ${component.type} нет переменной ${match[2]}`
  );
}

function validateCondition(condition, machine, platform, location) {
  if (!condition || condition === 'else') return;
  const match = /^(.+) (==|!=|>|<|>=|<=) (.+)$/.exec(condition);
  invariant(match, `${location}: условие не разобрано: ${condition}`);
  validateOperand(match[1], machine, platform, location);
  validateOperand(match[3], machine, platform, location);
}

const expectedMachines = [
  'ArduinoUno1',
  'MS1GateSensors',
  'MS1MainProtocol',
  'MS1GateActuator',
];
invariant(
  JSON.stringify(Object.keys(parsed.stateMachines)) === JSON.stringify(expectedMachines),
  'Неожиданный набор автоматов'
);

for (const [machineId, machine] of Object.entries(parsed.stateMachines)) {
  const platformFile = platformFiles[machine.platform];
  invariant(platformFile, `${machineId}: неизвестная платформа ${machine.platform}`);
  const platform = JSON.parse(
    fs.readFileSync(path.join(lapki, 'resources/platform', platformFile), 'utf8')
  );

  for (const component of Object.values(machine.components)) {
    const prototype = platform.components[component.type];
    invariant(prototype, `${machineId}: тип компонента ${component.type} отсутствует в платформе`);
    for (const [name, parameter] of Object.entries(prototype.constructorParameters ?? {})) {
      if (!parameter.optional) {
        invariant(
          component.parameters[name] !== undefined,
          `${machineId}: у ${component.id} отсутствует обязательный параметр ${name}`
        );
      }
    }
  }

  for (const [stateId, state] of Object.entries(machine.states)) {
    for (const action of state.actions) {
      validateTrigger(action.trigger?.event, machine, platform, `${machineId}.${stateId}`);
      validateCondition(action.trigger?.condition, machine, platform, `${machineId}.${stateId}`);
      if (action.action) validateAction(action.action, machine, platform, `${machineId}.${stateId}`);
    }
  }
  for (const [edgeId, transition] of Object.entries(machine.transitions)) {
    for (const action of transition.actions) {
      validateTrigger(action.trigger?.event, machine, platform, `${machineId}.${edgeId}`);
      validateCondition(action.trigger?.condition, machine, platform, `${machineId}.${edgeId}`);
      if (action.action) validateAction(action.action, machine, platform, `${machineId}.${edgeId}`);
    }
  }
}

for (let index = 0; index < 29; index += 1) {
  invariant(
    graphml.includes(`MainTelemetry.set(${index},`),
    `Телеметрия не заполняет позицию ${index}`
  );
}
invariant(graphml.includes('MainExpected.set(8);'), 'Нет восьмибайтового PROPOSE_PID');
invariant(graphml.includes('MainExpected.set(6);'), 'Нет шестибайтового PROPOSE_WEIGHTS');
invariant(graphml.includes('MainResponseExpected.set(21);'), 'Нет 21-байтового ответа state');
invariant(graphml.includes('MainResponseExpected.set(18);'), 'Нет 18-байтового ответа config');
invariant(
  !graphml.includes('packetReceived[MainBus.lastData != 3]') &&
    !graphml.includes('packetReceived[ActArduino.lastData != 3]'),
  'Полезная нагрузка всё ещё использует OK как разделитель'
);

console.log(
  `Validated ${expectedMachines.length} machines, platform components/actions, fixed response lengths and all 29 telemetry positions`
);
