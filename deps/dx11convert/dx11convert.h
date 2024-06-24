#ifndef DX11_CONVERT_H
#define DX11_CONVERT_H

#include <windows.h>
#include <d3d11.h>

class DX11ShaderNV12
{
  public:
    DX11ShaderNV12(ID3D11Device* d3ddevice, ID3D11DeviceContext* device_ctx);
    ~DX11ShaderNV12();

    void prepare_resources(int width, int height);
    void release_resources();
    HRESULT process_shader_nv12(ID3D11Texture2D* input_texture, ID3D11Texture2D** output_texture);
    void release_input_texture();

  private:
    HRESULT InitShiftWidthTexture(const UINT width);
    HRESULT InitRenderTargetLuma(const UINT width, const UINT height);
    HRESULT InitRenderTargetChromaCb(const UINT width, const UINT height);
    HRESULT InitRenderTargetChromaCr(const UINT width, const UINT height);
    HRESULT InitRenderTargetChromaBDownSampled(const UINT width, const UINT height);
    HRESULT InitRenderTargetChromaCDownSampled(const UINT width, const UINT height);
    HRESULT InitRenderTargetFakeNV12(const UINT width, const UINT height);

    void InitViewPort(const UINT, const UINT);
    void ProcessYCbCrShader();
    void ProcessChromaDownSampledShader();
    void ProcessYFakeNV12Shader();
    void ProcessFakeUVShader();


    int m_iViewportWidth = 0;
    int m_iViewportHeight = 0;
    ID3D11Device* m_pD3D11Device = NULL;
    ID3D11DeviceContext* m_pD3D11DeviceContext = NULL;

    ID3D11InputLayout* m_pVertexLayout = NULL;
	ID3D11SamplerState* m_pSamplerPointState = NULL;
    ID3D11SamplerState* m_pSamplerLinearState = NULL;

	ID3D11RenderTargetView* m_pLumaRT = NULL;
    ID3D11ShaderResourceView* m_pLumaRSV = NULL;
    ID3D11RenderTargetView* m_pChromaCBRT = NULL;
    ID3D11RenderTargetView* m_pChromaCRRT = NULL;
    ID3D11RenderTargetView* m_pChromaCBDownSampledRT = NULL;
    ID3D11RenderTargetView* m_pChromaCRDownSampledRT = NULL;
    ID3D11RenderTargetView* m_pFakeNV12RT = NULL;

    ID3D11ShaderResourceView* m_pInputRSV = NULL;
    ID3D11ShaderResourceView* m_pChromaCBRSV = NULL;
    ID3D11ShaderResourceView* m_pChromaCRRSV = NULL;
    ID3D11ShaderResourceView* m_pChromaCBDownSampledRSV = NULL;
    ID3D11ShaderResourceView* m_pChromaCRDownSampledRSV = NULL;
    ID3D11ShaderResourceView* m_pShiftWidthRSV = NULL;

    ID3D11VertexShader* m_pVertexShader = NULL;
    ID3D11VertexShader* m_pCombinedUVVertexShader = NULL;

    ID3D11PixelShader* m_pPixelShader2 = NULL;
    ID3D11PixelShader* m_pYCbCrShader2 = NULL;
    ID3D11PixelShader* m_pCombinedUVPixelShader = NULL;

};

#endif