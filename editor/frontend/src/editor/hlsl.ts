// ============================================================
//  HLSL の色分け (Monaco の Monarch) と、DXC のメッセージの読み取り
//
//  Monaco に依存しない (Vitest で試す)。登録は monaco.ts。
// ============================================================

// 型 (スカラー / ベクトル / 行列、テクスチャ、バッファ、サンプラー)
const scalarTypes = ['bool', 'int', 'uint', 'dword', 'half', 'float', 'double', 'min16float', 'min10float', 'min16int', 'min12int', 'min16uint', 'int16_t', 'uint16_t', 'int64_t', 'uint64_t', 'float16_t', 'float32_t', 'float64_t'];
const vectorTypes = scalarTypes.flatMap((t) => [1, 2, 3, 4].map((n) => `${t}${n}`));
const matrixTypes = scalarTypes.flatMap((t) => [1, 2, 3, 4].flatMap((r) => [1, 2, 3, 4].map((c) => `${t}${r}x${c}`)));
const objectTypes = [
  'void', 'vector', 'matrix', 'string',
  'Texture1D', 'Texture1DArray', 'Texture2D', 'Texture2DArray', 'Texture2DMS', 'Texture2DMSArray', 'Texture3D', 'TextureCube', 'TextureCubeArray',
  'RWTexture1D', 'RWTexture1DArray', 'RWTexture2D', 'RWTexture2DArray', 'RWTexture3D',
  'Buffer', 'RWBuffer', 'ByteAddressBuffer', 'RWByteAddressBuffer', 'StructuredBuffer', 'RWStructuredBuffer',
  'AppendStructuredBuffer', 'ConsumeStructuredBuffer', 'ConstantBuffer', 'RaytracingAccelerationStructure',
  'SamplerState', 'SamplerComparisonState', 'sampler', 'sampler2D', 'texture',
];
export const HLSL_TYPES = [...scalarTypes, ...vectorTypes, ...matrixTypes, ...objectTypes];

export const HLSL_KEYWORDS = [
  'break', 'case', 'cbuffer', 'tbuffer', 'const', 'continue', 'default', 'discard', 'do', 'else', 'extern', 'false', 'for',
  'groupshared', 'if', 'in', 'inline', 'inout', 'out', 'nointerpolation', 'noperspective', 'centroid', 'sample', 'linear',
  'precise', 'return', 'register', 'packoffset', 'row_major', 'column_major', 'shared', 'static', 'struct', 'switch',
  'true', 'typedef', 'uniform', 'volatile', 'while', 'namespace', 'template', 'typename', 'export', 'globallycoherent',
  'triangle', 'line', 'point', 'lineadj', 'triangleadj', 'unorm', 'snorm',
];

export const HLSL_INTRINSICS = [
  'abs', 'acos', 'all', 'any', 'asdouble', 'asfloat', 'asin', 'asint', 'asuint', 'atan', 'atan2', 'ceil', 'clamp', 'clip',
  'cos', 'cosh', 'countbits', 'cross', 'ddx', 'ddx_coarse', 'ddx_fine', 'ddy', 'ddy_coarse', 'ddy_fine', 'degrees', 'determinant',
  'distance', 'dot', 'exp', 'exp2', 'f16tof32', 'f32tof16', 'faceforward', 'firstbithigh', 'firstbitlow', 'floor', 'fma', 'fmod',
  'frac', 'frexp', 'fwidth', 'isfinite', 'isinf', 'isnan', 'ldexp', 'length', 'lerp', 'lit', 'log', 'log10', 'log2', 'mad', 'max',
  'min', 'modf', 'mul', 'normalize', 'pow', 'radians', 'rcp', 'reflect', 'refract', 'reversebits', 'round', 'rsqrt', 'saturate',
  'sign', 'sin', 'sincos', 'sinh', 'smoothstep', 'sqrt', 'step', 'tan', 'tanh', 'transpose', 'trunc',
  'GroupMemoryBarrier', 'GroupMemoryBarrierWithGroupSync', 'DeviceMemoryBarrier', 'DeviceMemoryBarrierWithGroupSync',
  'AllMemoryBarrier', 'AllMemoryBarrierWithGroupSync', 'InterlockedAdd', 'InterlockedAnd', 'InterlockedCompareExchange',
  'InterlockedCompareStore', 'InterlockedExchange', 'InterlockedMax', 'InterlockedMin', 'InterlockedOr', 'InterlockedXor',
  'WaveActiveSum', 'WaveActiveMax', 'WaveActiveMin', 'WaveGetLaneIndex', 'WaveReadLaneFirst', 'WaveIsFirstLane',
  'Sample', 'SampleLevel', 'SampleGrad', 'SampleBias', 'SampleCmp', 'SampleCmpLevelZero', 'Load', 'Store', 'Gather',
  'GetDimensions', 'Load2', 'Load3', 'Load4', 'Store2', 'Store3', 'Store4',
];

// Monaco の IMonarchLanguage と ILanguageConfiguration (型は monaco.ts で付ける)
export const hlslMonarch = {
  defaultToken: '',
  tokenPostfix: '.hlsl',
  keywords: HLSL_KEYWORDS,
  types: HLSL_TYPES,
  intrinsics: HLSL_INTRINSICS,
  operators: ['=', '>', '<', '!', '~', '?', ':', '==', '<=', '>=', '!=', '&&', '||', '++', '--', '+', '-', '*', '/', '&', '|', '^', '%', '<<', '>>', '+=', '-=', '*=', '/=', '&=', '|=', '^=', '%=', '<<=', '>>='],
  symbols: /[=><!~?:&|+\-*/^%]+/,
  tokenizer: {
    root: [
      // プリプロセッサー (#include "x" / #define …)
      [/^\s*#\s*include/, { token: 'keyword.directive', next: '@include' }],
      [/^\s*#\s*\w+/, 'keyword.directive'],
      // [numthreads(8, 8, 1)] などの属性
      [/\[\s*(numthreads|unroll|loop|branch|flatten|earlydepthstencil|domain|partitioning|outputtopology|outputcontrolpoints|patchconstantfunc|maxvertexcount|shader|RootSignature)\b/, 'annotation'],
      // セマンティクス (: SV_Target0 / : TEXCOORD0)
      [/(:\s*)(SV_\w+|[A-Z][A-Z_]*\d*)\b/, ['delimiter', 'type.semantic']],
      // register(t0) / register(b1, space0)
      [/(register|packoffset)(\s*\(\s*)([tbsu]\d+|c\d+(?:\.[xyzw])?)/, ['keyword', 'delimiter.parenthesis', 'number.register']],
      [/[a-zA-Z_]\w*/, { cases: { '@keywords': 'keyword', '@types': 'type', '@intrinsics': 'predefined', '@default': 'identifier' } }],
      { include: '@whitespace' },
      [/[{}()[\]]/, '@brackets'],
      [/@symbols/, { cases: { '@operators': 'operator', '@default': '' } }],
      [/\d*\.\d+([eE][-+]?\d+)?[fFhHlL]?/, 'number.float'],
      [/\d+\.\d*([eE][-+]?\d+)?[fFhHlL]?/, 'number.float'],
      [/0[xX][0-9a-fA-F]+[uUlL]*/, 'number.hex'],
      [/\d+[uUlL]*/, 'number'],
      [/[;,.]/, 'delimiter'],
      [/"([^"\\]|\\.)*$/, 'string.invalid'],
      [/"/, 'string', '@string'],
    ],
    include: [
      [/\s*"[^"]*"/, 'string', '@pop'],
      [/\s*<[^>]*>/, 'string', '@pop'],
      [/$/, '', '@pop'],
    ],
    whitespace: [
      [/[ \t\r\n]+/, ''],
      [/\/\*/, 'comment', '@comment'],
      [/\/\/.*$/, 'comment'],
    ],
    comment: [
      [/[^/*]+/, 'comment'],
      [/\*\//, 'comment', '@pop'],
      [/[/*]/, 'comment'],
    ],
    string: [
      [/[^\\"]+/, 'string'],
      [/\\./, 'string.escape'],
      [/"/, 'string', '@pop'],
    ],
  },
};

export const hlslLanguageConfiguration = {
  comments: { lineComment: '//', blockComment: ['/*', '*/'] as [string, string] },
  brackets: [['{', '}'], ['[', ']'], ['(', ')']] as [string, string][],
  autoClosingPairs: [
    { open: '{', close: '}' },
    { open: '[', close: ']' },
    { open: '(', close: ')' },
    { open: '"', close: '"', notIn: ['string'] },
    { open: '/*', close: ' */', notIn: ['string'] },
  ],
  surroundingPairs: [
    { open: '{', close: '}' },
    { open: '[', close: ']' },
    { open: '(', close: ')' },
    { open: '"', close: '"' },
  ],
};

// ---- DXC のメッセージ ----
export interface ShaderDiagnostic {
  file: string; // メッセージに書かれたファイル (区切りは "/")
  line: number; // 1 から
  column: number; // 1 から
  severity: 'error' | 'warning' | 'note';
  message: string;
}

// "D:\p\shaders\a.ps.hlsl:15:61: error: expected ';' after expression" の形の行を読む。
// 続く行 (ソースの抜粋と ^) は読み飛ばす
export function parseShaderMessages(text: string): ShaderDiagnostic[] {
  const out: ShaderDiagnostic[] = [];
  const re = /^(.*?):(\d+):(\d+):\s*(fatal error|error|warning|note):\s*(.*)$/;
  for (const raw of text.split(/\r?\n/)) {
    const m = re.exec(raw.trim());
    if (!m) continue;
    out.push({
      file: m[1].replace(/\\/g, '/'),
      line: Number(m[2]),
      column: Number(m[3]),
      severity: m[4] === 'warning' ? 'warning' : m[4] === 'note' ? 'note' : 'error',
      message: m[5],
    });
  }
  return out;
}

// メッセージのファイルが、アセットのパス ("shaders/a.ps.hlsl") と同じファイルか (絶対パスの末尾で比べる)
export function sameShaderFile(diagFile: string, assetPath: string): boolean {
  const a = diagFile.toLowerCase();
  const b = assetPath.replace(/\\/g, '/').toLowerCase();
  return a === b || a.endsWith('/' + b);
}
