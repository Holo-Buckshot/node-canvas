/* eslint-env mocha */

'use strict'

/**
 * The native addon is context-aware: it can be loaded in the main thread and
 * in any number of worker_threads Workers at the same time.
 */

const assert = require('assert')
const path = require('path')
const { Worker } = require('worker_threads')

const canvasPath = path.join(__dirname, '..')

// Draws with most of the API surface that keeps native per-environment state
// (class templates, DOMMatrix, the font parser, gradients, patterns,
// ImageData, async encoding) and resolves with a hash of the result.
const drawScript = `
  const { createCanvas, createImageData, Image } = require(${JSON.stringify(canvasPath)})
  const crypto = require('crypto')

  async function draw () {
    const canvas = createCanvas(64, 64)
    const ctx = canvas.getContext('2d')

    const grad = ctx.createLinearGradient(0, 0, 64, 64)
    grad.addColorStop(0, 'red')
    grad.addColorStop(1, 'blue')
    ctx.fillStyle = grad
    ctx.fillRect(0, 0, 64, 64)

    const tile = createCanvas(4, 4)
    const tctx = tile.getContext('2d')
    tctx.fillStyle = 'lime'
    tctx.fillRect(0, 0, 2, 2)
    ctx.fillStyle = ctx.createPattern(tile, 'repeat')
    ctx.fillRect(8, 8, 16, 16)

    ctx.setTransform(1, 0, 0, 1, 2, 3)
    const m = ctx.currentTransform
    if (m.e !== 2 || m.f !== 3) throw new Error('bad currentTransform')
    ctx.currentTransform = m

    ctx.font = 'bold 12px sans-serif'
    if (!/bold/.test(ctx.font)) throw new Error('font not parsed')
    ctx.fillStyle = 'black'
    ctx.fillText('hi', 30, 40)

    const id = createImageData(4, 4)
    id.data.fill(255)
    ctx.putImageData(id, 0, 0)
    const read = ctx.getImageData(0, 0, 4, 4)
    if (read.width !== 4 || read.data[0] !== 255) throw new Error('bad ImageData')

    const img = new Image()
    img.src = canvas.toBuffer('image/png')
    if (img.width !== 64) throw new Error('bad Image')
    ctx.drawImage(img, 0, 0, 32, 32)

    const png = await new Promise((resolve, reject) => {
      canvas.toBuffer((err, buf) => err ? reject(err) : resolve(buf))
    })
    const raw = canvas.toBuffer('raw')
    return {
      raw: crypto.createHash('sha1').update(raw).digest('hex'),
      pngBytes: png.length
    }
  }
`

function runInWorker (extra = '') {
  return new Promise((resolve, reject) => {
    const worker = new Worker(`
      const { parentPort } = require('worker_threads')
      ${drawScript}
      ${extra}
      draw().then(r => parentPort.postMessage(r), e => { throw e })
    `, { eval: true })
    let result
    worker.once('message', r => { result = r })
    worker.once('error', reject)
    worker.once('exit', code => {
      if (code !== 0) reject(new Error(`worker exited with code ${code}`))
      else resolve(result)
    })
  })
}

async function runInMain () {
  // eslint-disable-next-line no-new-func
  const draw = new Function('require', drawScript + '; return draw')(require)
  return draw()
}

describe('worker_threads', function () {
  this.timeout(30000)

  it('loads and draws in a worker', async function () {
    const expected = await runInMain()
    const actual = await runInWorker()
    assert.deepStrictEqual(actual, expected)
  })

  it('works in many workers at once and the main thread is unaffected', async function () {
    const expected = await runInMain()
    const results = await Promise.all(Array.from({ length: 8 }, () => runInWorker()))
    for (const r of results) assert.deepStrictEqual(r, expected)
    // Handles in the main thread must still be valid after workers exit.
    assert.deepStrictEqual(await runInMain(), expected)
  })

  it('survives workers being created and torn down repeatedly', async function () {
    for (let i = 0; i < 20; i++) await runInWorker()
  })

  it('survives workers being terminated mid-render', async function () {
    const workers = Array.from({ length: 4 }, () => new Worker(`
      ${drawScript}
      ;(async () => { for (;;) await draw() })()
    `, { eval: true }))
    await new Promise(resolve => setTimeout(resolve, 300))
    await Promise.all(workers.map(w => w.terminate()))
    await runInMain()
  })

  it('rejects prototype accessors called on a foreign receiver', function () {
    const { Image, createCanvas } = require('../')
    assert.throws(() => Object.create(Image.prototype).width, TypeError)
    // A real native object of another class must not be unwrapped as an Image.
    const canvas = Object.setPrototypeOf(createCanvas(1, 1), Image.prototype)
    assert.throws(() => canvas.width, TypeError)
    assert.throws(() => Image.prototype.width, TypeError)
  })
})
