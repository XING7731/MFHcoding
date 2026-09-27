/**
 * components/trend-chart/index.js
 * 温湿度趋势曲线（Canvas 2D 自绘，无第三方依赖）。
 *
 * 属性：
 *   xAxis      横轴标签数组，如 ['16:08:01','16:08:04', ...]
 *   series     数据系列 [{ name, data: number[], color }]
 *   height     画布高度（px），默认 240
 *   emptyText  无数据时的占位文案
 *   smooth     是否使用平滑曲线，默认 true
 */

const PADDING = { left: 42, right: 14, top: 34, bottom: 26 };
const Y_TICKS = 4;

function getPixelRatio() {
  try {
    if (typeof wx.getWindowInfo === 'function') {
      return wx.getWindowInfo().pixelRatio || 2;
    }
    return wx.getSystemInfoSync().pixelRatio || 2;
  } catch (err) {
    return 2;
  }
}

Component({
  properties: {
    xAxis: {
      type: Array,
      value: [],
      observer() {
        this.handleChange();
      },
    },
    series: {
      type: Array,
      value: [],
      observer() {
        this.handleChange();
      },
    },
    height: {
      type: Number,
      value: 240,
      observer() {
        this.resetCanvas();
        this.handleChange();
      },
    },
    emptyText: {
      type: String,
      value: '暂无温湿度数据',
    },
    smooth: {
      type: Boolean,
      value: true,
    },
  },

  data: {
    hasData: false,
  },

  lifetimes: {
    ready() {
      this.handleChange();
    },
    detached() {
      if (this.drawTimer) {
        clearTimeout(this.drawTimer);
        this.drawTimer = null;
      }
      this.resetCanvas();
    },
  },

  methods: {
    /* ---------------- 生命周期辅助 ---------------- */

    resetCanvas() {
      this.canvas = null;
      this.ctx = null;
      this.canvasWidth = 0;
      this.canvasHeight = 0;
    },

    /** 数据变化入口：切换空态 / 触发重绘 */
    handleChange() {
      const list = this.data.series || [];
      const hasData = list.some(
        (item) => item && Array.isArray(item.data) && item.data.length > 0
      );

      if (hasData !== this.data.hasData) {
        this.resetCanvas();
        this.setData({ hasData }, () => {
          if (hasData) {
            this.scheduleDraw();
          }
        });
        return;
      }
      if (hasData) {
        this.scheduleDraw();
      }
    },

    /** 80ms 防抖，避免高频轮询时反复重绘 */
    scheduleDraw() {
      if (this.drawTimer) {
        clearTimeout(this.drawTimer);
      }
      this.drawTimer = setTimeout(() => {
        this.drawTimer = null;
        this.draw();
      }, 80);
    },

    /* ---------------- Canvas 准备 ---------------- */

    ensureCanvas() {
      if (this.canvas && this.ctx) {
        return Promise.resolve({
          ctx: this.ctx,
          width: this.canvasWidth,
          height: this.canvasHeight,
        });
      }
      return new Promise((resolve) => {
        wx.createSelectorQuery()
          .in(this)
          .select('#trend-canvas')
          .fields({ node: true, size: true })
          .exec((res) => {
            const info = res && res[0];
            if (!info || !info.node || !info.width) {
              resolve(null);
              return;
            }
            const dpr = getPixelRatio();
            const canvas = info.node;
            canvas.width = info.width * dpr;
            canvas.height = info.height * dpr;
            const ctx = canvas.getContext('2d');
            ctx.scale(dpr, dpr);

            this.canvas = canvas;
            this.ctx = ctx;
            this.canvasWidth = info.width;
            this.canvasHeight = info.height;

            resolve({ ctx, width: info.width, height: info.height });
          });
      });
    },

    /* ---------------- 绘制 ---------------- */

    draw() {
      this.ensureCanvas().then((target) => {
        if (!target) {
          return;
        }
        const { ctx, width, height } = target;
        ctx.clearRect(0, 0, width, height);
        // 背景
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, width, height);

        const series = (this.data.series || []).filter(
          (item) => item && Array.isArray(item.data) && item.data.length > 0
        );
        if (series.length === 0) {
          return;
        }

        const pointCount = series.reduce(
          (max, item) => Math.max(max, item.data.length),
          0
        );

        const plotLeft = PADDING.left;
        const plotTop = PADDING.top;
        const plotWidth = Math.max(10, width - PADDING.left - PADDING.right);
        const plotHeight = Math.max(10, height - PADDING.top - PADDING.bottom);

        const range = computeRange(series);
        const xAt = (index) =>
          pointCount <= 1
            ? plotLeft + plotWidth / 2
            : plotLeft + (plotWidth * index) / (pointCount - 1);
        const yAt = (value) =>
          plotTop + plotHeight - ((value - range.min) / (range.max - range.min)) * plotHeight;

        this.drawGrid(ctx, {
          plotLeft,
          plotTop,
          plotWidth,
          plotHeight,
          range,
        });

        series.forEach((item) => {
          this.drawSeries(ctx, {
            item,
            xAt,
            yAt,
            plotLeft,
            plotTop,
            plotWidth,
            plotHeight,
          });
        });

        this.drawXAxis(ctx, { xAt, pointCount, plotTop, plotHeight });
        this.drawLegend(ctx, { series });
      });
    },

    /** 横向网格线 + Y 轴刻度 */
    drawGrid(ctx, { plotLeft, plotTop, plotWidth, plotHeight, range }) {
      ctx.save();
      ctx.strokeStyle = '#eef1f6';
      ctx.lineWidth = 1;
      ctx.fillStyle = '#9aa4b0';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';

      for (let i = 0; i <= Y_TICKS; i += 1) {
        const ratio = i / Y_TICKS;
        const y = plotTop + plotHeight * ratio;
        const value = range.max - (range.max - range.min) * ratio;

        ctx.beginPath();
        ctx.moveTo(plotLeft, Math.round(y) + 0.5);
        ctx.lineTo(plotLeft + plotWidth, Math.round(y) + 0.5);
        ctx.stroke();

        ctx.fillText(formatTick(value), plotLeft - 6, y);
      }
      ctx.restore();
    },

    /** 单条折线（含渐变面积、数据点） */
    drawSeries(ctx, { item, xAt, yAt, plotTop, plotHeight }) {
      const values = item.data.map((v) => Number(v));
      const color = item.color || '#1976d2';

      ctx.save();

      // 渐变面积
      const gradient = ctx.createLinearGradient(0, plotTop, 0, plotTop + plotHeight);
      gradient.addColorStop(0, hexToRgba(color, 0.22));
      gradient.addColorStop(1, hexToRgba(color, 0.01));
      ctx.beginPath();
      values.forEach((value, index) => {
        const x = xAt(index);
        const y = yAt(value);
        if (index === 0) {
          ctx.moveTo(x, y);
        } else if (this.data.smooth && index > 0) {
          const prevX = xAt(index - 1);
          const prevY = yAt(values[index - 1]);
          const cx = (prevX + x) / 2;
          ctx.bezierCurveTo(cx, prevY, cx, y, x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });
      const lastX = xAt(values.length - 1);
      ctx.lineTo(lastX, plotTop + plotHeight);
      ctx.lineTo(xAt(0), plotTop + plotHeight);
      ctx.closePath();
      ctx.fillStyle = gradient;
      ctx.fill();

      // 折线
      ctx.beginPath();
      values.forEach((value, index) => {
        const x = xAt(index);
        const y = yAt(value);
        if (index === 0) {
          ctx.moveTo(x, y);
        } else if (this.data.smooth) {
          const prevX = xAt(index - 1);
          const prevY = yAt(values[index - 1]);
          const cx = (prevX + x) / 2;
          ctx.bezierCurveTo(cx, prevY, cx, y, x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      ctx.stroke();

      // 数据点：只画最后一个，避免密集时糊成一团
      const lastIndex = values.length - 1;
      const lx = xAt(lastIndex);
      const ly = yAt(values[lastIndex]);
      ctx.beginPath();
      ctx.arc(lx, ly, 4.5, 0, Math.PI * 2);
      ctx.fillStyle = '#ffffff';
      ctx.fill();
      ctx.lineWidth = 2.5;
      ctx.strokeStyle = color;
      ctx.stroke();

      ctx.restore();
    },

    /** 横轴时间标签：最多显示 4 个，避免互相重叠 */
    drawXAxis(ctx, { xAt, pointCount, plotTop, plotHeight }) {
      const labels = this.data.xAxis || [];
      if (!labels.length) {
        return;
      }
      const maxLabels = 4;
      const step = Math.max(1, Math.ceil(pointCount / maxLabels));

      ctx.save();
      ctx.fillStyle = '#9aa4b0';
      ctx.font = '10px sans-serif';
      ctx.textBaseline = 'top';

      const indexes = [];
      for (let i = 0; i < pointCount; i += step) {
        indexes.push(i);
      }
      const lastIndex = pointCount - 1;
      if (indexes[indexes.length - 1] !== lastIndex) {
        indexes.push(lastIndex);
      }

      indexes.forEach((index, order) => {
        const text = labels[index];
        if (!text) {
          return;
        }
        let align = 'center';
        if (order === 0) {
          align = 'left';
        } else if (order === indexes.length - 1) {
          align = 'right';
        }
        ctx.textAlign = align;
        ctx.fillText(text, xAt(index), plotTop + plotHeight + 8);
      });
      ctx.restore();
    },

    /** 顶部图例：色点 + 名称 + 最新值 */
    drawLegend(ctx, { series }) {
      ctx.save();
      ctx.font = '11px sans-serif';
      ctx.textBaseline = 'middle';

      let offsetX = PADDING.left;
      const y = 14;

      series.forEach((item) => {
        const color = item.color || '#1976d2';
        const values = item.data.map((v) => Number(v));
        const latest = values[values.length - 1];
        const latestText = isFinite(latest) ? trim(latest) : '--';
        const unit = item.unit || '';
        const text = `${item.name} ${latestText}${unit}`;

        ctx.beginPath();
        ctx.arc(offsetX + 4, y, 4, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();

        ctx.textAlign = 'left';
        ctx.fillStyle = '#5b6673';
        ctx.fillText(text, offsetX + 14, y);

        offsetX += 14 + ctx.measureText(text).width + 18;
      });

      ctx.restore();
    },
  },
});

/* -------------------- 纯函数工具 -------------------- */

function computeRange(series) {
  let min = Infinity;
  let max = -Infinity;

  series.forEach((item) => {
    item.data.forEach((raw) => {
      const value = Number(raw);
      if (!isFinite(value)) {
        return;
      }
      min = Math.min(min, value);
      max = Math.max(max, value);
    });
  });

  if (!isFinite(min) || !isFinite(max)) {
    return { min: 0, max: 1 };
  }
  if (min === max) {
    min -= 1;
    max += 1;
  }

  const span = max - min;
  min = min - span * 0.15;
  max = max + span * 0.15;

  if (max - min < 2) {
    const mid = (max + min) / 2;
    min = mid - 1;
    max = mid + 1;
  }
  return { min, max };
}

function formatTick(value) {
  return Math.abs(value) >= 100 ? String(Math.round(value)) : value.toFixed(1);
}

function trim(value) {
  return Math.abs(value) >= 100 ? String(Math.round(value)) : value.toFixed(1);
}

/** '#1976d2' -> 'rgba(25,118,210,0.2)' */
function hexToRgba(hex, alpha) {
  const normalized = String(hex).replace('#', '');
  const full =
    normalized.length === 3
      ? normalized
          .split('')
          .map((c) => c + c)
          .join('')
      : normalized;
  const r = parseInt(full.slice(0, 2), 16);
  const g = parseInt(full.slice(2, 4), 16);
  const b = parseInt(full.slice(4, 6), 16);
  if ([r, g, b].some((n) => isNaN(n))) {
    return `rgba(25,118,210,${alpha})`;
  }
  return `rgba(${r},${g},${b},${alpha})`;
}
