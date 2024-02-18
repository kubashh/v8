// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export abstract class View {
    protected htmlParentElement: HTMLElement;
    protected htmlElement: HTMLElement;
    protected abstract createViewElement(): HTMLElement;

    constructor(htmlParentElement: HTMLElement) {
        this.htmlParentElement = htmlParentElement;
        this.htmlElement = this.createViewElement();
    }

    public show(): void {
        this.htmlParentElement.appendChild(this.htmlElement);
    }
    
    public hide(): void {
        this.htmlParentElement.removeChild(this.htmlElement);
    }
}

export abstract class PhaseView extends View {
    constructor(htmlParentElement: HTMLElement) {
        super(htmlParentElement);
    }
}