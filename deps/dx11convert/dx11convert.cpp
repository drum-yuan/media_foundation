#include "dx11convert.h"
#include <directxcolors.h>
#include "ScreenVS.inc"
#include "CombinedUVVS.inc"
#include "ScreenPS2.inc"
#include "YCbCrPS2.inc"
#include "CombinedUVPS.inc"

DX11ShaderNV12::DX11ShaderNV12(ID3D11Device* d3ddevice, ID3D11DeviceContext* device_ctx)
{
    m_pD3D11Device = d3ddevice;
    m_pD3D11DeviceContext = device_ctx;

    m_pD3D11Device->CreateVertexShader(g_ScreenVS, sizeof(g_ScreenVS), NULL, &m_pVertexShader);
    m_pD3D11Device->CreateVertexShader(g_CombinedUVVS, sizeof(g_CombinedUVVS), NULL, &m_pCombinedUVVertexShader);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    UINT numElements = ARRAYSIZE(layout);
    m_pD3D11Device->CreateInputLayout(layout, numElements, g_CombinedUVVS, sizeof(g_CombinedUVVS), &m_pVertexLayout);

    m_pD3D11Device->CreatePixelShader(g_ScreenPS2, sizeof(g_ScreenPS2), NULL, &m_pPixelShader2);
    m_pD3D11Device->CreatePixelShader(g_YCbCrPS2, sizeof(g_YCbCrPS2), NULL, &m_pYCbCrShader2);
    m_pD3D11Device->CreatePixelShader(g_CombinedUVPS, sizeof(g_CombinedUVPS), NULL, &m_pCombinedUVPixelShader);
}

DX11ShaderNV12::~DX11ShaderNV12()
{
    if (m_pVertexShader)
    {
		m_pVertexShader->Release();
	}
    if (m_pCombinedUVVertexShader)
    {
        m_pCombinedUVVertexShader->Release();
    }
    if (m_pPixelShader2)
    {
		m_pPixelShader2->Release();
	}
    if (m_pYCbCrShader2)
    {
		m_pYCbCrShader2->Release();
    }
    if (m_pCombinedUVPixelShader)
    {
		m_pCombinedUVPixelShader->Release();
	}
}

void DX11ShaderNV12::prepare_resources(int width, int height)
{
    InitShiftWidthTexture(width);
    InitRenderTargetLuma(width, height);
    InitRenderTargetChromaCb(width, height);
    InitRenderTargetChromaCr(width, height);
    InitRenderTargetChromaBDownSampled(width, height);
    InitRenderTargetChromaCDownSampled(width, height);
    InitRenderTargetFakeNV12(width, height);

	D3D11_SAMPLER_DESC SampDesc;
    ZeroMemory(&SampDesc, sizeof(SampDesc));
    SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    SampDesc.MinLOD = 0;
    SampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    m_pD3D11Device->CreateSamplerState(&SampDesc, &m_pSamplerPointState);

    SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    m_pD3D11Device->CreateSamplerState(&SampDesc, &m_pSamplerLinearState);

    InitViewPort(width, height);

    m_pD3D11DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_pD3D11DeviceContext->IASetInputLayout(m_pVertexLayout);
    m_pD3D11DeviceContext->PSSetSamplers(0, 1, &m_pSamplerPointState);

    m_iViewportWidth = width;
    m_iViewportHeight = height;
}

void DX11ShaderNV12::release_resources()
{
    if (m_pLumaRT)
    {
        m_pLumaRT->Release();
    }
    if (m_pLumaRSV)
    {
		m_pLumaRSV->Release();
	}
    if (m_pChromaCBRT)
    {
		m_pChromaCBRT->Release();
	}
    if (m_pChromaCRRT)
    {
		m_pChromaCRRT->Release();
	}
    if (m_pChromaCBDownSampledRT)
    {
		m_pChromaCBDownSampledRT->Release();
	}
    if (m_pChromaCRDownSampledRT)
    {
		m_pChromaCRDownSampledRT->Release();
	}
    if (m_pFakeNV12RT)
    {
		m_pFakeNV12RT->Release();
	}

    if (m_pInputRSV)
    {
        m_pInputRSV->Release();
	}
    if (m_pChromaCBRSV)
    {
		m_pChromaCBRSV->Release();
	}
    if (m_pChromaCRRSV)
    {
		m_pChromaCRRSV->Release();
	}
    if (m_pChromaCBDownSampledRSV)
    {
		m_pChromaCBDownSampledRSV->Release();
	}
    if (m_pChromaCRDownSampledRSV)
    {
		m_pChromaCRDownSampledRSV->Release();
	}
    if (m_pShiftWidthRSV)
    {
		m_pShiftWidthRSV->Release();
	}

    if (m_pSamplerPointState)
    {
		m_pSamplerPointState->Release();
	}
    if (m_pSamplerLinearState)
    {
		m_pSamplerLinearState->Release();
    }
}

HRESULT DX11ShaderNV12::process_shader_nv12(ID3D11Texture2D* input_texture, ID3D11Texture2D** output_texture)
{
    if (m_pD3D11Device == NULL)
    {
		return E_FAIL;
	}

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2D;
    srvDesc2D.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc2D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc2D.Texture2D.MipLevels = 1;
    srvDesc2D.Texture2D.MostDetailedMip = 0;
    m_pD3D11Device->CreateShaderResourceView(input_texture, &srvDesc2D, &m_pInputRSV);

    ProcessYCbCrShader();

    InitViewPort(m_iViewportWidth / 2, m_iViewportHeight / 2);
    m_pD3D11DeviceContext->PSSetSamplers(0, 1, &m_pSamplerLinearState);
    ProcessChromaDownSampledShader();

    InitViewPort(m_iViewportWidth, m_iViewportHeight);
    m_pD3D11DeviceContext->PSSetSamplers(0, 1, &m_pSamplerPointState);
    ProcessYFakeNV12Shader();

    InitViewPort(m_iViewportWidth, m_iViewportHeight * 2);
    ProcessFakeUVShader();

    InitViewPort(m_iViewportWidth, m_iViewportHeight);

    if (m_pFakeNV12RT == NULL)
    {
        return E_FAIL;
    }
    m_pFakeNV12RT->GetResource(reinterpret_cast<ID3D11Resource**>(output_texture));
    return S_OK;
}

void DX11ShaderNV12::release_input_texture()
{
    if (m_pInputRSV)
	{
		m_pInputRSV->Release();
	}
}

void DX11ShaderNV12::InitViewPort(const UINT width, const UINT height)
{
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;

    m_pD3D11DeviceContext->RSSetViewports(1, &vp);
}

void DX11ShaderNV12::ProcessYCbCrShader()
{
    ID3D11RenderTargetView* pYCbCrRT[3];
    pYCbCrRT[0] = m_pLumaRT;
    pYCbCrRT[1] = m_pChromaCBRT;
    pYCbCrRT[2] = m_pChromaCRRT;

    m_pD3D11DeviceContext->OMSetRenderTargets(3, pYCbCrRT, NULL);
    m_pD3D11DeviceContext->ClearRenderTargetView(pYCbCrRT[0], DirectX::Colors::Aquamarine);
    m_pD3D11DeviceContext->ClearRenderTargetView(pYCbCrRT[1], DirectX::Colors::Aquamarine);
    m_pD3D11DeviceContext->ClearRenderTargetView(pYCbCrRT[2], DirectX::Colors::Aquamarine);
    m_pD3D11DeviceContext->VSSetShader(m_pVertexShader, NULL, 0);
    m_pD3D11DeviceContext->PSSetShader(m_pYCbCrShader2, NULL, 0);
    m_pD3D11DeviceContext->PSSetShaderResources(0, 1, &m_pInputRSV);
    m_pD3D11DeviceContext->Draw(4, 0);
    m_pD3D11DeviceContext->Flush();
}

void DX11ShaderNV12::ProcessChromaDownSampledShader()
{
    ID3D11RenderTargetView* pChromaRT[2];
    pChromaRT[0] = m_pChromaCBDownSampledRT;
    pChromaRT[1] = m_pChromaCRDownSampledRT;

    ID3D11ShaderResourceView* pChromaRSV[2];
    pChromaRSV[0] = m_pChromaCBRSV;
    pChromaRSV[1] = m_pChromaCRRSV;

    m_pD3D11DeviceContext->OMSetRenderTargets(2, pChromaRT, NULL);
    m_pD3D11DeviceContext->ClearRenderTargetView(pChromaRT[0], DirectX::Colors::Aquamarine);
    m_pD3D11DeviceContext->ClearRenderTargetView(pChromaRT[1], DirectX::Colors::Aquamarine);
    m_pD3D11DeviceContext->VSSetShader(m_pVertexShader, NULL, 0);
    m_pD3D11DeviceContext->PSSetShader(m_pPixelShader2, NULL, 0);
    m_pD3D11DeviceContext->PSSetShaderResources(0, 2, pChromaRSV);
    m_pD3D11DeviceContext->Draw(4, 0);
    m_pD3D11DeviceContext->Flush();
}

void DX11ShaderNV12::ProcessYFakeNV12Shader()
{
    m_pD3D11DeviceContext->OMSetRenderTargets(1, &m_pFakeNV12RT, NULL);
    m_pD3D11DeviceContext->ClearRenderTargetView(m_pFakeNV12RT, DirectX::Colors::Aquamarine);
    m_pD3D11DeviceContext->VSSetShader(m_pVertexShader, NULL, 0);
    m_pD3D11DeviceContext->PSSetShader(m_pPixelShader2, NULL, 0);
    m_pD3D11DeviceContext->PSSetShaderResources(0, 1, &m_pLumaRSV);
    m_pD3D11DeviceContext->Draw(4, 0);
    m_pD3D11DeviceContext->Flush();
}

void DX11ShaderNV12::ProcessFakeUVShader()
{
    m_pD3D11DeviceContext->VSSetShader(m_pCombinedUVVertexShader, NULL, 0);
    m_pD3D11DeviceContext->PSSetShader(m_pCombinedUVPixelShader, NULL, 0);
    m_pD3D11DeviceContext->PSSetShaderResources(0, 1, &m_pChromaCBDownSampledRSV);
    m_pD3D11DeviceContext->PSSetShaderResources(1, 1, &m_pChromaCRDownSampledRSV);
    m_pD3D11DeviceContext->PSSetShaderResources(2, 1, &m_pShiftWidthRSV);
    m_pD3D11DeviceContext->Draw(4, 0);
    m_pD3D11DeviceContext->Flush();
}

HRESULT DX11ShaderNV12::InitShiftWidthTexture(const UINT uiWidth)
{
    HRESULT hr = S_OK;
    ID3D11Texture1D* pInTexture1D = NULL;
    D3D11_SUBRESOURCE_DATA SubResource1D;
    BYTE* pShiftData = NULL;

    D3D11_TEXTURE1D_DESC desc1D;
    desc1D.Width = uiWidth;
    desc1D.MipLevels = 1;
    desc1D.ArraySize = 1;
    desc1D.Format = DXGI_FORMAT_R8_UNORM;
    desc1D.Usage = D3D11_USAGE_DEFAULT;
    desc1D.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc1D.CPUAccessFlags = 0;
    desc1D.MiscFlags = 0;

    pShiftData = new BYTE[uiWidth];
    BYTE* pData = pShiftData;

    for (UINT ui = 0; ui < uiWidth; ui++)
    {
        if (ui % 2)
            *pData++ = 0x01;
        else
            *pData++ = 0x00;
    }

    ZeroMemory(&SubResource1D, sizeof(SubResource1D));
    SubResource1D.pSysMem = (void*)pShiftData;
    SubResource1D.SysMemPitch = uiWidth;

    m_pD3D11Device->CreateTexture1D(&desc1D, &SubResource1D, &pInTexture1D);
    if (pInTexture1D == NULL)
    {
        return E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc1D;
    srvDesc1D.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc1D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
    srvDesc1D.Texture2D.MipLevels = 1;
    srvDesc1D.Texture2D.MostDetailedMip = 0;

    m_pD3D11Device->CreateShaderResourceView(pInTexture1D, &srvDesc1D, &m_pShiftWidthRSV);

    delete[] pShiftData;
	pInTexture1D->Release();
    return hr;
}

HRESULT DX11ShaderNV12::InitRenderTargetLuma(const UINT width, const UINT height)
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pTexture2D = NULL;

    D3D11_TEXTURE2D_DESC desc2D;
    desc2D.Width = width;
    desc2D.Height = height;
    desc2D.MipLevels = 1;
    desc2D.ArraySize = 1;
    desc2D.Format = DXGI_FORMAT_R8_UNORM;
    desc2D.SampleDesc.Count = 1;
    desc2D.SampleDesc.Quality = 0;
    desc2D.Usage = D3D11_USAGE_DEFAULT;
    desc2D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc2D.CPUAccessFlags = 0;
    desc2D.MiscFlags = 0;

    m_pD3D11Device->CreateTexture2D(&desc2D, NULL, &pTexture2D);
    if (pTexture2D == NULL)
    {
        return E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2D;
    srvDesc2D.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc2D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc2D.Texture2D.MipLevels = 1;
    srvDesc2D.Texture2D.MostDetailedMip = 0;

    m_pD3D11Device->CreateShaderResourceView(pTexture2D, &srvDesc2D, &m_pLumaRSV);

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    m_pD3D11Device->CreateRenderTargetView(pTexture2D, &rtDesc, &m_pLumaRT);

	pTexture2D->Release();
    return hr;
}

HRESULT DX11ShaderNV12::InitRenderTargetChromaCb(const UINT width, const UINT height)
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pTexture2D = NULL;

    D3D11_TEXTURE2D_DESC desc2D;
    desc2D.Width = width;
    desc2D.Height = height;
    desc2D.MipLevels = 1;
    desc2D.ArraySize = 1;
    desc2D.Format = DXGI_FORMAT_R8_UNORM;
    desc2D.SampleDesc.Count = 1;
    desc2D.SampleDesc.Quality = 0;
    desc2D.Usage = D3D11_USAGE_DEFAULT;
    desc2D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc2D.CPUAccessFlags = 0;
    desc2D.MiscFlags = 0;

    m_pD3D11Device->CreateTexture2D(&desc2D, NULL, &pTexture2D);
    if (pTexture2D == NULL)
    {
        return E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2D;
    srvDesc2D.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc2D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc2D.Texture2D.MipLevels = 1;
    srvDesc2D.Texture2D.MostDetailedMip = 0;

    m_pD3D11Device->CreateShaderResourceView(pTexture2D, &srvDesc2D, &m_pChromaCBRSV);

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    m_pD3D11Device->CreateRenderTargetView(pTexture2D, &rtDesc, &m_pChromaCBRT);

	pTexture2D->Release();
    return hr;
}

HRESULT DX11ShaderNV12::InitRenderTargetChromaCr(const UINT width, const UINT height)
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pTexture2D = NULL;

    D3D11_TEXTURE2D_DESC desc2D;
    desc2D.Width = width;
    desc2D.Height = height;
    desc2D.MipLevels = 1;
    desc2D.ArraySize = 1;
    desc2D.Format = DXGI_FORMAT_R8_UNORM;
    desc2D.SampleDesc.Count = 1;
    desc2D.SampleDesc.Quality = 0;
    desc2D.Usage = D3D11_USAGE_DEFAULT;
    desc2D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc2D.CPUAccessFlags = 0;
    desc2D.MiscFlags = 0;

    m_pD3D11Device->CreateTexture2D(&desc2D, NULL, &pTexture2D);
    if (pTexture2D == NULL)
    {
        return E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2D;
    srvDesc2D.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc2D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc2D.Texture2D.MipLevels = 1;
    srvDesc2D.Texture2D.MostDetailedMip = 0;

    m_pD3D11Device->CreateShaderResourceView(pTexture2D, &srvDesc2D, &m_pChromaCRRSV);

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    m_pD3D11Device->CreateRenderTargetView(pTexture2D, &rtDesc, &m_pChromaCRRT);

    pTexture2D->Release();
    return hr;
}

HRESULT DX11ShaderNV12::InitRenderTargetChromaBDownSampled(const UINT width, const UINT height)
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pTexture2D = NULL;

    D3D11_TEXTURE2D_DESC desc2D;
    desc2D.Width = width / 2;
    desc2D.Height = height / 2;
    desc2D.MipLevels = 1;
    desc2D.ArraySize = 1;
    desc2D.Format = DXGI_FORMAT_R8_UNORM;
    desc2D.SampleDesc.Count = 1;
    desc2D.SampleDesc.Quality = 0;
    desc2D.Usage = D3D11_USAGE_DEFAULT;
    desc2D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc2D.CPUAccessFlags = 0;
    desc2D.MiscFlags = 0;

    m_pD3D11Device->CreateTexture2D(&desc2D, NULL, &pTexture2D);
    if (pTexture2D == NULL)
    {
        return E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2D;
    srvDesc2D.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc2D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc2D.Texture2D.MipLevels = 1;
    srvDesc2D.Texture2D.MostDetailedMip = 0;

    m_pD3D11Device->CreateShaderResourceView(pTexture2D, &srvDesc2D, &m_pChromaCBDownSampledRSV);

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    m_pD3D11Device->CreateRenderTargetView(pTexture2D, &rtDesc, &m_pChromaCBDownSampledRT);

	pTexture2D->Release();
    return hr;
}

HRESULT DX11ShaderNV12::InitRenderTargetChromaCDownSampled(const UINT width, const UINT height)
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pTexture2D = NULL;

    D3D11_TEXTURE2D_DESC desc2D;
    desc2D.Width = width / 2;
    desc2D.Height = height / 2;
    desc2D.MipLevels = 1;
    desc2D.ArraySize = 1;
    desc2D.Format = DXGI_FORMAT_R8_UNORM;
    desc2D.SampleDesc.Count = 1;
    desc2D.SampleDesc.Quality = 0;
    desc2D.Usage = D3D11_USAGE_DEFAULT;
    desc2D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc2D.CPUAccessFlags = 0;
    desc2D.MiscFlags = 0;

    m_pD3D11Device->CreateTexture2D(&desc2D, NULL, &pTexture2D);
    if (pTexture2D == NULL)
    {
        return E_FAIL;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc2D;
    srvDesc2D.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc2D.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc2D.Texture2D.MipLevels = 1;
    srvDesc2D.Texture2D.MostDetailedMip = 0;

    m_pD3D11Device->CreateShaderResourceView(pTexture2D, &srvDesc2D, &m_pChromaCRDownSampledRSV);

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    m_pD3D11Device->CreateRenderTargetView(pTexture2D, &rtDesc, &m_pChromaCRDownSampledRT);

    pTexture2D->Release();
    return hr;
}

HRESULT DX11ShaderNV12::InitRenderTargetFakeNV12(const UINT width, const UINT height)
{
    HRESULT hr = S_OK;
    ID3D11Texture2D* pTexture2D = NULL;

    D3D11_TEXTURE2D_DESC desc2D;
    desc2D.Width = width;
    desc2D.Height = height + (height / 2);
    desc2D.MipLevels = 1;
    desc2D.ArraySize = 1;
    desc2D.Format = DXGI_FORMAT_R8_UNORM;
    desc2D.SampleDesc.Count = 1;
    desc2D.SampleDesc.Quality = 0;
    desc2D.Usage = D3D11_USAGE_DEFAULT;
    desc2D.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc2D.CPUAccessFlags = 0;
    desc2D.MiscFlags = 0;

    m_pD3D11Device->CreateTexture2D(&desc2D, NULL, &pTexture2D);
    if (pTexture2D == NULL)
    {
        return E_FAIL;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    m_pD3D11Device->CreateRenderTargetView(pTexture2D, &rtDesc, &m_pFakeNV12RT);

    pTexture2D->Release();
    return hr;
}