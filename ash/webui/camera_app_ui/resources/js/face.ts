// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assertInstanceof, assertNotReached} from './assert.js';
import * as dom from './dom.js';
import {Resolution} from './type.js';

// G-yellow-600 with alpha = 0.8
const RECT_COLOR = 'rgba(249, 171, 0, 0.8)';

/**
 * Rotates the given coordinates in [0, 1] square space by the given
 * orientation.
 * @return The rotated [x, y].
 */
function rotate(x: number, y: number, orientation: number): [number, number] {
  switch (orientation) {
    case 0:
      return [x, y];
    case 90:
      return [y, 1.0 - x];
    case 180:
      return [1.0 - x, 1.0 - y];
    case 270:
      return [1.0 - y, x];
  }
  assertNotReached('Unexpected orientation');
}

/**
 * An overlay to show face rectangles over preview.
 */
export class FaceOverlay {
  private readonly canvas = dom.get('#preview-face-overlay', HTMLCanvasElement);
  private readonly ctx: CanvasRenderingContext2D;

  /**
   * @param orientation Counter-clockwise angles to apply rotation to
   *     the face rectangles.
   */
  constructor(
      private readonly activeArraySize: Resolution,
      private readonly orientation: number) {
    this.ctx = assertInstanceof(
        this.canvas.getContext('2d'), CanvasRenderingContext2D);
  }

  /**
   * Shows the given rectangles on overlay. The old rectangles would be
   * cleared, if any.
   * @param rects An array of [x1, y1, x2, y2] to represent rectangles in the
   *     coordinate system of active array in sensor.
   */
  show(rects: number[]): void {
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

    // TODO(b/178344897): Handle zoomed preview.
    // TODO(b/178344897): Handle screen orientation dynamically.

    this.ctx.strokeStyle = RECT_COLOR;
    for (let i = 0; i < rects.length; i += 4) {
      let [x1, y1, x2, y2] = rects.slice(i, i + 4);
      x1 /= this.activeArraySize.width;
      y1 /= this.activeArraySize.height;
      x2 /= this.activeArraySize.width;
      y2 /= this.activeArraySize.height;
      [x1, y1] = rotate(x1, y1, this.orientation);
      [x2, y2] = rotate(x2, y2, this.orientation);

      const canvasAspectRatio = this.canvas.width / this.canvas.height;
      const sensorAspectRatio =
          this.activeArraySize.width / this.activeArraySize.height;
      if (canvasAspectRatio > sensorAspectRatio) {
        // Canvas has wider aspect than the sensor, e.g. when we're showing a
        // 16:9 stream captured from a 4:3 sensor. Based on our hardware
        // requirement, we assume the stream is cropped into letterbox from the
        // active array.
        const normalizedCanvasHeight = sensorAspectRatio / canvasAspectRatio;
        const clipped = (1 - normalizedCanvasHeight) / 2;
        x1 *= this.canvas.width;
        y1 = (Math.max(y1 - clipped, 0) / normalizedCanvasHeight) *
            this.canvas.height;
        x2 *= this.canvas.width;
        y2 = (Math.max(y2 - clipped, 0) / normalizedCanvasHeight) *
            this.canvas.height;
      } else if (canvasAspectRatio < sensorAspectRatio) {
        // Canvas has taller aspect than the sensor, e.g. when we're showing a
        // 4:3 stream captured from a 16:9 sensor. Based on our hardware
        // requirement, we assume the stream is cropped into pillarbox from the
        // active array.
        const normalizedCanvasWidth = canvasAspectRatio / sensorAspectRatio;
        const clipped = (1 - normalizedCanvasWidth) / 2;
        x1 = (Math.max(x1 - clipped, 0) * normalizedCanvasWidth) *
            this.canvas.width;
        y1 *= this.canvas.height;
        x2 = (Math.max(x2 - clipped, 0) * normalizedCanvasWidth) *
            this.canvas.width;
        y2 *= this.canvas.height;
      }
      this.ctx.strokeRect(x1, y1, x2 - x1, y2 - y1);
    }
  }

  /**
   * Clears all rectangles.
   */
  clear(): void {
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
  }
}
