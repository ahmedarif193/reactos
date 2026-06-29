#pragma once

/*
 * The upstream helper header includes <winddi.h>, but this driver only needs
 * the WinGDI/DDI scalar types already provided by the earlier helper includes.
 * Pulling ReactOS' full winddi.h also drags in legacy DirectDraw declarations
 * that collide with the modern WDDM path we are trying to compile.
 */
