/**
  @file   cz_env.cpp
  @brief  VS環境で実行されるコンソールのコードページを変更する
  @author 植山沙欧
  @date   2026.02.10
**/
#include<sstream>
#include<ostream>
#include<print>

//! cz_ext名前空間を定義。cz_extはcz科のextensionを意味する
namespace cz_ext
{
  /**
    @struct CZ_Environment
    @brief  コンソールの文字コードを設定するためのクラス
             コンストラクタでシステムコマンドを実行して、コンソールのコードページを変更する
  **/
  struct CZ_Environment 
  {
    //! コンストラクタでコンソールのコードページを設定する
    CZ_Environment(const std::string codepageNo)
    {
      std::ostringstream command;
      std::print(command, "chcp {} > nul", codepageNo); //! コマンドを構築する
      std::system(command.str().c_str()); //! コードページを変更する
    }
    //! デストラクタで実行ファイル終了時にポーズをかける
    ~CZ_Environment()
    {
      std::system("pause > nul");
    }
  };
  static CZ_Environment cz_environment("65001"); //!コンソールのコードページを65001（UTF-8）に変更
}